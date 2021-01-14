/* SPDX-License-Identifier: GPL-2.0 */
/*
 * These are the MPEG2 state controls for use with stateless MPEG-2
 * codec drivers.
 *
 * It turns out that these structs are not stable yet and will undergo
 * more changes. So keep them private until they are stable and ready to
 * become part of the official public API.
 */

#ifndef _MPEG2_CTRLS_H_
#define _MPEG2_CTRLS_H_

#define V4L2_CTRL_CLASS_CODEC           0x00990000      /* Stateful codec controls */
#define V4L2_CID_CODEC_BASE                     (V4L2_CTRL_CLASS_CODEC | 0x900)

#define V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS		(V4L2_CID_CODEC_BASE+250)
#define V4L2_CID_MPEG_VIDEO_MPEG2_SEQUENCE		(V4L2_CID_CODEC_BASE+251)
#define V4L2_CID_MPEG_VIDEO_MPEG2_PICTURE		(V4L2_CID_CODEC_BASE+252)
#define V4L2_CID_MPEG_VIDEO_MPEG2_QUANTIZATION		(V4L2_CID_CODEC_BASE+253)

/* enum v4l2_ctrl_type type values */
#define V4L2_CTRL_TYPE_MPEG2_SLICE_PARAMS 0x0130
#define V4L2_CTRL_TYPE_MPEG2_SEQUENCE 0x0131
#define V4L2_CTRL_TYPE_MPEG2_PICTURE 0x0132
#define V4L2_CTRL_TYPE_MPEG2_QUANTIZATION 0x0133

#define V4L2_MPEG2_SEQ_FLAG_PROGRESSIVE		0x0001

/**
 * struct v4l2_ctrl_mpeg2_sequence - MPEG-2 sequence header
 *
 * All the members on this structure match the sequence header and sequence
 * extension syntaxes as specified by the MPEG-2 specification.
 *
 * Fields horizontal_size, vertical_size and vbv_buffer_size are a
 * combination of respective _value and extension syntax elements,
 * as described in section 6.3.3 "Sequence header".
 *
 * @horizontal_size: combination of elements horizontal_size_value and
 * horizontal_size_extension.
 * @vertical_size: combination of elements vertical_size_value and
 * vertical_size_extension.
 * @vbv_buffer_size: combination of elements vbv_buffer_size_value and
 * vbv_buffer_size_extension.
 * @profile_and_level_indication: see MPEG-2 specification.
 * @chroma_format: see MPEG-2 specification.
 * @reserved: padding field. Should be zeroed by applications.
 * @flags: see V4L2_MPEG2_SEQ_FLAG_{}.
 */
struct v4l2_ctrl_mpeg2_sequence {
	__u16	horizontal_size;
	__u16	vertical_size;
	__u32	vbv_buffer_size;
	__u16	profile_and_level_indication;
	__u8	chroma_format;
	__u8	reserved;
	__u32	flags;
};

#define V4L2_MPEG2_PIC_CODING_TYPE_I			1
#define V4L2_MPEG2_PIC_CODING_TYPE_P			2
#define V4L2_MPEG2_PIC_CODING_TYPE_B			3
#define V4L2_MPEG2_PIC_CODING_TYPE_D			4

#define V4L2_MPEG2_PIC_TOP_FIELD			0x1
#define V4L2_MPEG2_PIC_BOTTOM_FIELD			0x2
#define V4L2_MPEG2_PIC_FRAME				0x3

#define V4L2_MPEG2_PIC_FLAG_TOP_FIELD_FIRST		0x0001
#define V4L2_MPEG2_PIC_FLAG_FRAME_PRED_DCT		0x0002
#define V4L2_MPEG2_PIC_FLAG_CONCEALMENT_MV		0x0004
#define V4L2_MPEG2_PIC_FLAG_Q_SCALE_TYPE		0x0008
#define V4L2_MPEG2_PIC_FLAG_INTRA_VLC			0x0010
#define V4L2_MPEG2_PIC_FLAG_ALT_SCAN			0x0020
#define V4L2_MPEG2_PIC_FLAG_REPEAT_FIRST		0x0040
#define V4L2_MPEG2_PIC_FLAG_PROGRESSIVE			0x0080
#define V4L2_MPEG2_PIC_FLAG_LOAD_INTRA			0x0100
#define V4L2_MPEG2_PIC_FLAG_LOAD_NON_INTRA		0x0200
#define V4L2_MPEG2_PIC_FLAG_LOAD_CHROMA_INTRA		0x0400
#define V4L2_MPEG2_PIC_FLAG_LOAD_CHROMA_NON_INTRA	0x0800

/**
 * struct v4l2_ctrl_mpeg2_picture - MPEG-2 picture header
 *
 * All the members on this structure match the picture header and picture
 * coding extension syntaxes as specified by the MPEG-2 specification.
 *
 * In particular, the set of quantization load flags V4L2_MPEG2_PIC_FLAG_LOAD_{}
 * are specified here in order to allow applications to pass non-default
 * quantization matrices. In this case, applications are expected to use
 * V4L2_CTRL_TYPE_MPEG2_QUANTIZATION to pass the values of non-default
 * matrices.
 *
 * @picture_coding_type: see MPEG-2 specification.
 * @f_code[2][2]: see MPEG-2 specification.
 * @intra_dc_precision: see MPEG-2 specification.
 * @picture_structure: see V4L2_MPEG2_PIC_{}_FIELD.
 * @reserved: padding field. Should be zeroed by applications.
 * @flags: see V4L2_MPEG2_PIC_FLAG_{}.
 */
struct v4l2_ctrl_mpeg2_picture {
	__u8	picture_coding_type;
	__u8	f_code[2][2];
	__u8	intra_dc_precision;
	__u8	picture_structure;
	__u8	reserved;
	__u32	flags;
};

/**
 * struct v4l2_ctrl_mpeg2_slice_params - MPEG-2 slice header
 *
 * @backward_ref_ts: timestamp of the V4L2 capture buffer to use as
 * reference for backward prediction.
 * @forward_ref_ts: timestamp of the V4L2 capture buffer to use as
 * reference for forward prediction. These timestamp refers to the
 * timestamp field in struct v4l2_buffer. Use v4l2_timeval_to_ns()
 * to convert the struct timeval to a __u64.
 * @quantiser_scale_code: quantiser scale integer matching an
 * homonymous syntax element.
 * @reserved: padding field. Should be zeroed by applications.
 */
struct v4l2_ctrl_mpeg2_slice_params {
	__u64	backward_ref_ts;
	__u64	forward_ref_ts;
	__u32	quantiser_scale_code;
	__u32	reserved;
};

/**
 * struct v4l2_ctrl_mpeg2_quantization - MPEG-2 quantization
 *
 * Quantization matrices as specified by section 6.3.7
 * "Quant matrix extension".
 *
 * Applications are expected to set the quantization matrices load
 * flags V4L2_MPEG2_PIC_FLAG_LOAD_{} in struct v4l2_ctrl_mpeg2_picture
 * to tell the kernel that a non-default matrix shall be used
 * to decode the picture.
 *
 * @intra_quantiser_matrix: The quantization matrix coefficients
 * for intra-coded frames, in zigzag scanning order. It is relevant
 * for both luma and chroma components, although it can be superseded
 * by the chroma-specific matrix for non-4:2:0 YUV formats.
 * @non_intra_quantiser_matrix: The quantization matrix coefficients
 * for non-intra-coded frames, in zigzag scanning order. It is relevant
 * for both luma and chroma components, although it can be superseded
 * by the chroma-specific matrix for non-4:2:0 YUV formats.
 * @chroma_intra_quantiser_matrix: The quantization matrix coefficients
 * for the chominance component of intra-coded frames, in zigzag scanning
 * order. Only relevant for 4:2:2 and 4:4:4 YUV formats.
 * @chroma_non_intra_quantiser_matrix: The quantization matrix coefficients
 * for the chrominance component of non-intra-coded frames, in zigzag scanning
 * order. Only relevant for 4:2:2 and 4:4:4 YUV formats.
 */
struct v4l2_ctrl_mpeg2_quantization {
	__u8	intra_quantiser_matrix[64];
	__u8	non_intra_quantiser_matrix[64];
	__u8	chroma_intra_quantiser_matrix[64];
	__u8	chroma_non_intra_quantiser_matrix[64];
};

#endif
