/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "hevcdec.h"
#include "hwaccel.h"
#include "v4l2_request.h"

typedef struct V4L2RequestControlsHEVC {
    struct v4l2_ctrl_hevc_sps sps;
    struct v4l2_ctrl_hevc_pps pps;
    struct v4l2_ctrl_hevc_scaling_matrix scaling_matrix;
    struct v4l2_ctrl_hevc_slice_params slice_params;
} V4L2RequestControlsHEVC;

static void v4l2_request_hevc_fill_pred_table(const HEVCContext *h, struct v4l2_hevc_pred_weight_table *table)
{
    int32_t luma_weight_denom, chroma_weight_denom;
    const SliceHeader *sh = &h->sh;

    if (sh->slice_type == HEVC_SLICE_I ||
        (sh->slice_type == HEVC_SLICE_P && !h->ps.pps->weighted_pred_flag) ||
        (sh->slice_type == HEVC_SLICE_B && !h->ps.pps->weighted_bipred_flag))
        return;

    table->luma_log2_weight_denom = sh->luma_log2_weight_denom;

    if (h->ps.sps->chroma_format_idc)
        table->delta_chroma_log2_weight_denom = sh->chroma_log2_weight_denom - sh->luma_log2_weight_denom;

    luma_weight_denom = (1 << sh->luma_log2_weight_denom);
    chroma_weight_denom = (1 << sh->chroma_log2_weight_denom);

    for (int i = 0; i < 15 && i < sh->nb_refs[L0]; i++) {
        table->delta_luma_weight_l0[i] = sh->luma_weight_l0[i] - luma_weight_denom;
        table->luma_offset_l0[i] = sh->luma_offset_l0[i];
        table->delta_chroma_weight_l0[i][0] = sh->chroma_weight_l0[i][0] - chroma_weight_denom;
        table->delta_chroma_weight_l0[i][1] = sh->chroma_weight_l0[i][1] - chroma_weight_denom;
        table->chroma_offset_l0[i][0] = sh->chroma_offset_l0[i][0];
        table->chroma_offset_l0[i][1] = sh->chroma_offset_l0[i][1];
    }

    if (sh->slice_type != HEVC_SLICE_B)
        return;

    for (int i = 0; i < 15 && i < sh->nb_refs[L1]; i++) {
        table->delta_luma_weight_l1[i] = sh->luma_weight_l1[i] - luma_weight_denom;
        table->luma_offset_l1[i] = sh->luma_offset_l1[i];
        table->delta_chroma_weight_l1[i][0] = sh->chroma_weight_l1[i][0] - chroma_weight_denom;
        table->delta_chroma_weight_l1[i][1] = sh->chroma_weight_l1[i][1] - chroma_weight_denom;
        table->chroma_offset_l1[i][0] = sh->chroma_offset_l1[i][0];
        table->chroma_offset_l1[i][1] = sh->chroma_offset_l1[i][1];
    }
}

static int find_frame_rps_type(const HEVCContext *h, int frame_buf_tag)
{
    const HEVCFrame *frame;
    int i;

    for (i = 0; i < h->rps[ST_CURR_BEF].nb_refs; i++) {
        frame = h->rps[ST_CURR_BEF].ref[i];
        if (frame && frame_buf_tag == ff_v4l2_request_get_capture_tag(frame->frame))
            return V4L2_HEVC_DPB_ENTRY_RPS_ST_CURR_BEFORE;
    }

    for (i = 0; i < h->rps[ST_CURR_AFT].nb_refs; i++) {
        frame = h->rps[ST_CURR_AFT].ref[i];
        if (frame && frame_buf_tag == ff_v4l2_request_get_capture_tag(frame->frame))
            return V4L2_HEVC_DPB_ENTRY_RPS_ST_CURR_AFTER;
    }

    for (i = 0; i < h->rps[LT_CURR].nb_refs; i++) {
        frame = h->rps[LT_CURR].ref[i];
        if (frame && frame_buf_tag == ff_v4l2_request_get_capture_tag(frame->frame))
            return V4L2_HEVC_DPB_ENTRY_RPS_LT_CURR;
    }

    return 0;
}

static uint8_t get_ref_pic_index(const HEVCContext *h, const HEVCFrame *frame,
                                 struct v4l2_ctrl_hevc_slice_params *slice_params)
{
    int frame_buf_tag;
    uint8_t i;

    if (!frame)
        return 0;

    frame_buf_tag = ff_v4l2_request_get_capture_tag(frame->frame);

    for (i = 0; i < slice_params->num_active_dpb_entries; i++) {
        int buf_tag = slice_params->dpb[i].buffer_tag;
        int poc = slice_params->dpb[i].pic_order_cnt[0];
        if (buf_tag == frame_buf_tag && poc == frame->poc)
            return i;
    }

    return 0;
}

static void v4l2_request_hevc_fill_slice_params(const HEVCContext *h,
                                                struct v4l2_ctrl_hevc_slice_params *slice_params)
{
    const HEVCFrame *pic = h->ref;
    const SliceHeader *sh = &h->sh;
    int i, entries = 0;
    RefPicList *rpl;

    *slice_params = (struct v4l2_ctrl_hevc_slice_params) {
        .bit_size = 0,
        .data_bit_offset = get_bits_count(&h->HEVClc->gb),

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: NAL unit header */
        .nal_unit_type = h->nal_unit_type,
        .nuh_temporal_id_plus1 = h->temporal_id + 1,

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
        .slice_type = sh->slice_type,
        .colour_plane_id = sh->colour_plane_id,
        .slice_pic_order_cnt = pic->poc,
        .slice_sao_luma_flag = sh->slice_sample_adaptive_offset_flag[0],
        .slice_sao_chroma_flag = sh->slice_sample_adaptive_offset_flag[1],
        .slice_temporal_mvp_enabled_flag = sh->slice_temporal_mvp_enabled_flag,
        .num_ref_idx_l0_active_minus1 = sh->nb_refs[L0] ? sh->nb_refs[L0] - 1 : 0,
        .num_ref_idx_l1_active_minus1 = sh->nb_refs[L1] ? sh->nb_refs[L1] - 1 : 0,
        .mvd_l1_zero_flag = sh->mvd_l1_zero_flag,
        .cabac_init_flag = sh->cabac_init_flag,
        .collocated_from_l0_flag = sh->collocated_list == L0 ? 1 : 0,
        .collocated_ref_idx = sh->slice_temporal_mvp_enabled_flag ? sh->collocated_ref_idx : 0,
        .five_minus_max_num_merge_cand = sh->slice_type == HEVC_SLICE_I ? 0 : 5 - sh->max_num_merge_cand,
        .use_integer_mv_flag = 0,
        .slice_qp_delta = sh->slice_qp_delta,
        .slice_cb_qp_offset = sh->slice_cb_qp_offset,
        .slice_cr_qp_offset = sh->slice_cr_qp_offset,
        .slice_act_y_qp_offset = 0,
        .slice_act_cb_qp_offset = 0,
        .slice_act_cr_qp_offset = 0,
        .slice_deblocking_filter_disabled_flag = sh->disable_deblocking_filter_flag,
        .slice_beta_offset_div2 = sh->beta_offset / 2,
        .slice_tc_offset_div2 = sh->tc_offset / 2,
        .slice_loop_filter_across_slices_enabled_flag = sh->slice_loop_filter_across_slices_enabled_flag,

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: Picture timing SEI message */
        .pic_struct = h->sei.picture_timing.picture_struct,

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
        .num_rps_poc_st_curr_before = h->rps[ST_CURR_BEF].nb_refs,
        .num_rps_poc_st_curr_after = h->rps[ST_CURR_AFT].nb_refs,
        .num_rps_poc_lt_curr = h->rps[LT_CURR].nb_refs,

        .slice_segment_addr = sh->slice_segment_addr,
    };

    for (i = 0; i < FF_ARRAY_ELEMS(h->DPB); i++) {
        const HEVCFrame *frame = &h->DPB[i];
        if (frame != pic && (frame->flags & (HEVC_FRAME_FLAG_LONG_REF | HEVC_FRAME_FLAG_SHORT_REF))) {
            struct v4l2_hevc_dpb_entry *entry = &slice_params->dpb[entries++];

            entry->buffer_tag = ff_v4l2_request_get_capture_tag(frame->frame);
            entry->rps = find_frame_rps_type(h, entry->buffer_tag);
            entry->field_pic = frame->frame->interlaced_frame;

            /* TODO: Interleaved: Get the POC for each field. */
            entry->pic_order_cnt[0] = frame->poc;
            entry->pic_order_cnt[1] = frame->poc;
        }
    }

    slice_params->num_active_dpb_entries = entries;

    if (sh->slice_type != HEVC_SLICE_I) {
        rpl = &h->ref->refPicList[0];
        for (i = 0; i < rpl->nb_refs; i++)
            slice_params->ref_idx_l0[i] = get_ref_pic_index(h, rpl->ref[i], slice_params);
    }

    if (sh->slice_type == HEVC_SLICE_B) {
        rpl = &h->ref->refPicList[1];
        for (i = 0; i < rpl->nb_refs; i++)
            slice_params->ref_idx_l1[i] = get_ref_pic_index(h, rpl->ref[i], slice_params);
    }

    v4l2_request_hevc_fill_pred_table(h, &slice_params->pred_weight_table);

    slice_params->num_entry_point_offsets = sh->num_entry_point_offsets;
    if (slice_params->num_entry_point_offsets > 256) {
        slice_params->num_entry_point_offsets = 256;
        av_log(NULL, AV_LOG_ERROR, "%s: Currently only 256 entry points are supported, but slice has %d entry points.\n", __func__, sh->num_entry_point_offsets);
    }

    for (i = 0; i < slice_params->num_entry_point_offsets; i++)
        slice_params->entry_point_offset_minus1[i] = sh->entry_point_offset[i] - 1;
}

static int v4l2_request_hevc_start_frame(AVCodecContext *avctx,
                                         av_unused const uint8_t *buffer,
                                         av_unused uint32_t size)
{
    const HEVCContext *h = avctx->priv_data;
    const HEVCSPS *sps = h->ps.sps;
    const HEVCPPS *pps = h->ps.pps;
    const ScalingList *sl = pps->scaling_list_data_present_flag ?
                            &pps->scaling_list :
                            sps->scaling_list_enable_flag ?
                            &sps->scaling_list : NULL;
    V4L2RequestControlsHEVC *controls = h->ref->hwaccel_picture_private;

    /* ISO/IEC 23008-2, ITU-T Rec. H.265: Sequence parameter set */
    controls->sps = (struct v4l2_ctrl_hevc_sps) {
        .chroma_format_idc = sps->chroma_format_idc,
        .separate_colour_plane_flag = sps->separate_colour_plane_flag,
        .pic_width_in_luma_samples = sps->width,
        .pic_height_in_luma_samples = sps->height,
        .bit_depth_luma_minus8 = sps->bit_depth - 8,
        .bit_depth_chroma_minus8 = sps->bit_depth - 8,
        .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_poc_lsb - 4,
        .sps_max_dec_pic_buffering_minus1 = sps->temporal_layer[sps->max_sub_layers - 1].max_dec_pic_buffering - 1,
        .sps_max_num_reorder_pics = sps->temporal_layer[sps->max_sub_layers - 1].num_reorder_pics,
        .sps_max_latency_increase_plus1 = sps->temporal_layer[sps->max_sub_layers - 1].max_latency_increase + 1,
        .log2_min_luma_coding_block_size_minus3 = sps->log2_min_cb_size - 3,
        .log2_diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_coding_block_size,
        .log2_min_luma_transform_block_size_minus2 = sps->log2_min_tb_size - 2,
        .log2_diff_max_min_luma_transform_block_size = sps->log2_max_trafo_size - sps->log2_min_tb_size,
        .max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter,
        .max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra,
        .scaling_list_enabled_flag = sps->scaling_list_enable_flag,
        .amp_enabled_flag = sps->amp_enabled_flag,
        .sample_adaptive_offset_enabled_flag = sps->sao_enabled,
        .pcm_enabled_flag = sps->pcm_enabled_flag,
        .pcm_sample_bit_depth_luma_minus1 = sps->pcm.bit_depth - 1,
        .pcm_sample_bit_depth_chroma_minus1 = sps->pcm.bit_depth_chroma - 1,
        .log2_min_pcm_luma_coding_block_size_minus3 = sps->pcm.log2_min_pcm_cb_size - 3,
        .log2_diff_max_min_pcm_luma_coding_block_size = sps->pcm.log2_max_pcm_cb_size - sps->pcm.log2_min_pcm_cb_size,
        .pcm_loop_filter_disabled_flag = sps->pcm.loop_filter_disable_flag,
        .num_short_term_ref_pic_sets = sps->nb_st_rps,
        .long_term_ref_pics_present_flag = sps->long_term_ref_pics_present_flag,
        .num_long_term_ref_pics_sps = sps->num_long_term_ref_pics_sps,
        .sps_temporal_mvp_enabled_flag = sps->sps_temporal_mvp_enabled_flag,
        .strong_intra_smoothing_enabled_flag = sps->sps_strong_intra_smoothing_enable_flag,
    };

    if (sl) {
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 16; j++)
                controls->scaling_matrix.scaling_list_4x4[i][j] = sl->sl[0][i][j];
            for (int j = 0; j < 64; j++) {
                controls->scaling_matrix.scaling_list_8x8[i][j]   = sl->sl[1][i][j];
                controls->scaling_matrix.scaling_list_16x16[i][j] = sl->sl[2][i][j];
                if (i < 2)
                    controls->scaling_matrix.scaling_list_32x32[i][j] = sl->sl[3][i * 3][j];
            }
            controls->scaling_matrix.scaling_list_dc_coef_16x16[i] = sl->sl_dc[0][i];
            if (i < 2)
                controls->scaling_matrix.scaling_list_dc_coef_32x32[i] = sl->sl_dc[1][i * 3];
        }
    }

    /* ISO/IEC 23008-2, ITU-T Rec. H.265: Picture parameter set */
    controls->pps = (struct v4l2_ctrl_hevc_pps) {
        .dependent_slice_segment_flag = pps->dependent_slice_segments_enabled_flag,
        .output_flag_present_flag = pps->output_flag_present_flag,
        .num_extra_slice_header_bits = pps->num_extra_slice_header_bits,
        .sign_data_hiding_enabled_flag = pps->sign_data_hiding_flag,
        .cabac_init_present_flag = pps->cabac_init_present_flag,
        .init_qp_minus26 = pps->pic_init_qp_minus26,
        .constrained_intra_pred_flag = pps->constrained_intra_pred_flag,
        .transform_skip_enabled_flag = pps->transform_skip_enabled_flag,
        .cu_qp_delta_enabled_flag = pps->cu_qp_delta_enabled_flag,
        .diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth,
        .pps_cb_qp_offset = pps->cb_qp_offset,
        .pps_cr_qp_offset = pps->cr_qp_offset,
        .pps_slice_chroma_qp_offsets_present_flag = pps->pic_slice_level_chroma_qp_offsets_present_flag,
        .weighted_pred_flag = pps->weighted_pred_flag,
        .weighted_bipred_flag = pps->weighted_bipred_flag,
        .transquant_bypass_enabled_flag = pps->transquant_bypass_enable_flag,
        .tiles_enabled_flag = pps->tiles_enabled_flag,
        .entropy_coding_sync_enabled_flag = pps->entropy_coding_sync_enabled_flag,
        .loop_filter_across_tiles_enabled_flag = pps->loop_filter_across_tiles_enabled_flag,
        .pps_loop_filter_across_slices_enabled_flag = pps->seq_loop_filter_across_slices_enabled_flag,
        .deblocking_filter_override_enabled_flag = pps->deblocking_filter_override_enabled_flag,
        .pps_disable_deblocking_filter_flag = pps->disable_dbf,
        .pps_beta_offset_div2 = pps->beta_offset / 2,
        .pps_tc_offset_div2 = pps->tc_offset / 2,
        .lists_modification_present_flag = pps->lists_modification_present_flag,
        .log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level - 2,
        .slice_segment_header_extension_present_flag = pps->slice_header_extension_present_flag,
        .scaling_list_enable_flag = pps->scaling_list_data_present_flag, // pps_scaling_list_data_present_flag
    };

    if (pps->tiles_enabled_flag) {
        controls->pps.num_tile_columns_minus1 = pps->num_tile_columns - 1;
        controls->pps.num_tile_rows_minus1 = pps->num_tile_rows - 1;

        av_log(avctx, AV_LOG_DEBUG, "%s: avctx=%p tiles_enabled_flag=%d num_tile_columns=%d num_tile_rows=%d\n", __func__, avctx, pps->tiles_enabled_flag, pps->num_tile_columns, pps->num_tile_rows);

        for (int i = 0; i < pps->num_tile_columns; i++)
            controls->pps.column_width_minus1[i] = pps->column_width[i] - 1;

        for (int i = 0; i < pps->num_tile_rows; i++)
            controls->pps.row_height_minus1[i] = pps->row_height[i] - 1;
    }

    return ff_v4l2_request_reset_frame(avctx, h->ref->frame);
}

static int v4l2_request_hevc_end_frame(AVCodecContext *avctx)
{
    const HEVCContext *h = avctx->priv_data;
    V4L2RequestControlsHEVC *controls = h->ref->hwaccel_picture_private;
    V4L2RequestDescriptor *req = (V4L2RequestDescriptor*)h->ref->frame->data[0];

    struct v4l2_ext_control control[] = {
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_SPS,
            .ptr = &controls->sps,
            .size = sizeof(controls->sps),
        },
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_PPS,
            .ptr = &controls->pps,
            .size = sizeof(controls->pps),
        },
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_SCALING_MATRIX,
            .ptr = &controls->scaling_matrix,
            .size = sizeof(controls->scaling_matrix),
        },
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_SLICE_PARAMS,
            .ptr = &controls->slice_params,
            .size = sizeof(controls->slice_params),
        },
    };

    controls->slice_params.bit_size = req->output.used * 8;

    return ff_v4l2_request_decode_frame(avctx, h->ref->frame, control, FF_ARRAY_ELEMS(control));
}

static int v4l2_request_hevc_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    const HEVCContext *h = avctx->priv_data;
    V4L2RequestControlsHEVC *controls = h->ref->hwaccel_picture_private;
    V4L2RequestDescriptor *req = (V4L2RequestDescriptor*)h->ref->frame->data[0];

    // HACK: trigger decode per slice
    if (req->output.used) {
        v4l2_request_hevc_end_frame(avctx);
        ff_v4l2_request_reset_frame(avctx, h->ref->frame);
    }

    v4l2_request_hevc_fill_slice_params(h, &controls->slice_params);

    return ff_v4l2_request_append_output_buffer(avctx, h->ref->frame, buffer, size);
}

static int v4l2_request_hevc_init(AVCodecContext *avctx)
{
    return ff_v4l2_request_init(avctx, V4L2_PIX_FMT_HEVC_SLICE, 1024 * 1024, NULL, 0);
}

const AVHWAccel ff_hevc_v4l2request_hwaccel = {
    .name           = "hevc_v4l2request",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVC,
    .pix_fmt        = AV_PIX_FMT_DRM_PRIME,
    .start_frame    = v4l2_request_hevc_start_frame,
    .decode_slice   = v4l2_request_hevc_decode_slice,
    .end_frame      = v4l2_request_hevc_end_frame,
    .frame_priv_data_size = sizeof(V4L2RequestControlsHEVC),
    .init           = v4l2_request_hevc_init,
    .uninit         = ff_v4l2_request_uninit,
    .priv_data_size = sizeof(V4L2RequestContext),
    .frame_params   = ff_v4l2_request_frame_params,
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
