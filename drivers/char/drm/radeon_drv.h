/* radeon_drv.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#ifndef __RADEON_DRV_H__
#define __RADEON_DRV_H__

#define GET_RING_HEAD(ring)		DRM_READ32(  (volatile u32 *) (ring)->head )
#define SET_RING_HEAD(ring,val)		DRM_WRITE32( (volatile u32 *) (ring)->head , (val))

typedef struct drm_radeon_freelist {
   	unsigned int age;
   	drm_buf_t *buf;
   	struct drm_radeon_freelist *next;
   	struct drm_radeon_freelist *prev;
} drm_radeon_freelist_t;

typedef struct drm_radeon_ring_buffer {
	u32 *start;
	u32 *end;
	int size;
	int size_l2qw;

	volatile u32 *head;
	u32 tail;
	u32 tail_mask;
	int space;

	int high_mark;
} drm_radeon_ring_buffer_t;

typedef struct drm_radeon_depth_clear_t {
	u32 rb3d_cntl;
	u32 rb3d_zstencilcntl;
	u32 se_cntl;
} drm_radeon_depth_clear_t;

typedef struct drm_radeon_private {
	drm_radeon_ring_buffer_t ring;
	drm_radeon_sarea_t *sarea_priv;

	int agp_size;
	u32 agp_vm_start;
	unsigned long agp_buffers_offset;

	int cp_mode;
	int cp_running;

   	drm_radeon_freelist_t *head;
   	drm_radeon_freelist_t *tail;
	int last_buf;
	volatile u32 *scratch;

	int usec_timeout;

	int is_r200;

	int is_pci;
	unsigned long phys_pci_gart;
	dma_addr_t bus_pci_gart;

	struct {
		u32 boxes;
		int freelist_timeouts;
		int freelist_loops;
		int requested_bufs;
		int last_frame_reads;
		int last_clear_reads;
		int clears;
		int texture_uploads;
	} stats;

	int do_boxes;
	int page_flipping;
	int current_page;
	u32 crtc_offset;
	u32 crtc_offset_cntl;

	u32 color_fmt;
	unsigned int front_offset;
	unsigned int front_pitch;
	unsigned int back_offset;
	unsigned int back_pitch;

	u32 depth_fmt;
	unsigned int depth_offset;
	unsigned int depth_pitch;

	u32 front_pitch_offset;
	u32 back_pitch_offset;
	u32 depth_pitch_offset;

	drm_radeon_depth_clear_t depth_clear;

	drm_map_t *sarea;
	drm_map_t *fb;
	drm_map_t *mmio;
	drm_map_t *cp_ring;
	drm_map_t *ring_rptr;
	drm_map_t *buffers;
	drm_map_t *agp_textures;
} drm_radeon_private_t;

typedef struct drm_radeon_buf_priv {
	u32 age;
} drm_radeon_buf_priv_t;

				/* radeon_cp.c */
extern int radeon_cp_init( DRM_IOCTL_ARGS );
extern int radeon_cp_start( DRM_IOCTL_ARGS );
extern int radeon_cp_stop( DRM_IOCTL_ARGS );
extern int radeon_cp_reset( DRM_IOCTL_ARGS );
extern int radeon_cp_idle( DRM_IOCTL_ARGS );
extern int radeon_engine_reset( DRM_IOCTL_ARGS );
extern int radeon_fullscreen( DRM_IOCTL_ARGS );
extern int radeon_cp_buffers( DRM_IOCTL_ARGS );

extern void radeon_freelist_reset( drm_device_t *dev );
extern drm_buf_t *radeon_freelist_get( drm_device_t *dev );

extern int radeon_wait_ring( drm_radeon_private_t *dev_priv, int n );

extern int radeon_do_cp_idle( drm_radeon_private_t *dev_priv );
extern int radeon_do_cleanup_cp( drm_device_t *dev );
extern int radeon_do_cleanup_pageflip( drm_device_t *dev );

				/* radeon_state.c */
extern int radeon_cp_clear( DRM_IOCTL_ARGS );
extern int radeon_cp_swap( DRM_IOCTL_ARGS );
extern int radeon_cp_vertex( DRM_IOCTL_ARGS );
extern int radeon_cp_indices( DRM_IOCTL_ARGS );
extern int radeon_cp_texture( DRM_IOCTL_ARGS );
extern int radeon_cp_stipple( DRM_IOCTL_ARGS );
extern int radeon_cp_indirect( DRM_IOCTL_ARGS );
extern int radeon_cp_vertex2( DRM_IOCTL_ARGS );
extern int radeon_cp_cmdbuf( DRM_IOCTL_ARGS );
extern int radeon_cp_getparam( DRM_IOCTL_ARGS );
extern int radeon_cp_flip( DRM_IOCTL_ARGS );

/* Flags for stats.boxes
 */
#define RADEON_BOX_DMA_IDLE      0x1
#define RADEON_BOX_RING_FULL     0x2
#define RADEON_BOX_FLIP          0x4
#define RADEON_BOX_WAIT_IDLE     0x8
#define RADEON_BOX_TEXTURE_LOAD  0x10



/* Register definitions, register access macros and drmAddMap constants
 * for Radeon kernel driver.
 */

#define RADEON_AGP_COMMAND		0x0f60
#define RADEON_AUX_SCISSOR_CNTL		0x26f0
#	define RADEON_EXCLUSIVE_SCISSOR_0	(1 << 24)
#	define RADEON_EXCLUSIVE_SCISSOR_1	(1 << 25)
#	define RADEON_EXCLUSIVE_SCISSOR_2	(1 << 26)
#	define RADEON_SCISSOR_0_ENABLE		(1 << 28)
#	define RADEON_SCISSOR_1_ENABLE		(1 << 29)
#	define RADEON_SCISSOR_2_ENABLE		(1 << 30)

#define RADEON_BUS_CNTL			0x0030
#	define RADEON_BUS_MASTER_DIS		(1 << 6)

#define RADEON_CLOCK_CNTL_DATA		0x000c
#	define RADEON_PLL_WR_EN			(1 << 7)
#define RADEON_CLOCK_CNTL_INDEX		0x0008
#define RADEON_CONFIG_APER_SIZE		0x0108
#define RADEON_CRTC_OFFSET		0x0224
#define RADEON_CRTC_OFFSET_CNTL		0x0228
#	define RADEON_CRTC_TILE_EN		(1 << 15)
#	define RADEON_CRTC_OFFSET_FLIP_CNTL	(1 << 16)

#define RADEON_RB3D_COLORPITCH		0x1c48

#define RADEON_DP_GUI_MASTER_CNTL	0x146c
#	define RADEON_GMC_SRC_PITCH_OFFSET_CNTL	(1 << 0)
#	define RADEON_GMC_DST_PITCH_OFFSET_CNTL	(1 << 1)
#	define RADEON_GMC_BRUSH_SOLID_COLOR	(13 << 4)
#	define RADEON_GMC_BRUSH_NONE		(15 << 4)
#	define RADEON_GMC_DST_16BPP		(4 << 8)
#	define RADEON_GMC_DST_24BPP		(5 << 8)
#	define RADEON_GMC_DST_32BPP		(6 << 8)
#	define RADEON_GMC_DST_DATATYPE_SHIFT	8
#	define RADEON_GMC_SRC_DATATYPE_COLOR	(3 << 12)
#	define RADEON_DP_SRC_SOURCE_MEMORY	(2 << 24)
#	define RADEON_DP_SRC_SOURCE_HOST_DATA	(3 << 24)
#	define RADEON_GMC_CLR_CMP_CNTL_DIS	(1 << 28)
#	define RADEON_GMC_WR_MSK_DIS		(1 << 30)
#	define RADEON_ROP3_S			0x00cc0000
#	define RADEON_ROP3_P			0x00f00000
#define RADEON_DP_WRITE_MASK		0x16cc
#define RADEON_DST_PITCH_OFFSET		0x142c
#define RADEON_DST_PITCH_OFFSET_C	0x1c80
#	define RADEON_DST_TILE_LINEAR		(0 << 30)
#	define RADEON_DST_TILE_MACRO		(1 << 30)
#	define RADEON_DST_TILE_MICRO		(2 << 30)
#	define RADEON_DST_TILE_BOTH		(3 << 30)

#define RADEON_SCRATCH_REG0		0x15e0
#define RADEON_SCRATCH_REG1		0x15e4
#define RADEON_SCRATCH_REG2		0x15e8
#define RADEON_SCRATCH_REG3		0x15ec
#define RADEON_SCRATCH_REG4		0x15f0
#define RADEON_SCRATCH_REG5		0x15f4
#define RADEON_SCRATCH_UMSK		0x0770
#define RADEON_SCRATCH_ADDR		0x0774

#define RADEON_HOST_PATH_CNTL		0x0130
#	define RADEON_HDP_SOFT_RESET		(1 << 26)
#	define RADEON_HDP_WC_TIMEOUT_MASK	(7 << 28)
#	define RADEON_HDP_WC_TIMEOUT_28BCLK	(7 << 28)

#define RADEON_ISYNC_CNTL		0x1724
#	define RADEON_ISYNC_ANY2D_IDLE3D	(1 << 0)
#	define RADEON_ISYNC_ANY3D_IDLE2D	(1 << 1)
#	define RADEON_ISYNC_TRIG2D_IDLE3D	(1 << 2)
#	define RADEON_ISYNC_TRIG3D_IDLE2D	(1 << 3)
#	define RADEON_ISYNC_WAIT_IDLEGUI	(1 << 4)
#	define RADEON_ISYNC_CPSCRATCH_IDLEGUI	(1 << 5)

#define RADEON_RBBM_GUICNTL		0x172c
#	define RADEON_HOST_DATA_SWAP_NONE	(0 << 0)
#	define RADEON_HOST_DATA_SWAP_16BIT	(1 << 0)
#	define RADEON_HOST_DATA_SWAP_32BIT	(2 << 0)
#	define RADEON_HOST_DATA_SWAP_HDW	(3 << 0)

#define RADEON_MC_AGP_LOCATION		0x014c
#define RADEON_MC_FB_LOCATION		0x0148
#define RADEON_MCLK_CNTL		0x0012
#	define RADEON_FORCEON_MCLKA		(1 << 16)
#	define RADEON_FORCEON_MCLKB		(1 << 17)
#	define RADEON_FORCEON_YCLKA		(1 << 18)
#	define RADEON_FORCEON_YCLKB		(1 << 19)
#	define RADEON_FORCEON_MC		(1 << 20)
#	define RADEON_FORCEON_AIC		(1 << 21)

#define RADEON_PP_BORDER_COLOR_0	0x1d40
#define RADEON_PP_BORDER_COLOR_1	0x1d44
#define RADEON_PP_BORDER_COLOR_2	0x1d48
#define RADEON_PP_CNTL			0x1c38
#	define RADEON_SCISSOR_ENABLE		(1 <<  1)
#define RADEON_PP_LUM_MATRIX		0x1d00
#define RADEON_PP_MISC			0x1c14
#define RADEON_PP_ROT_MATRIX_0		0x1d58
#define RADEON_PP_TXFILTER_0		0x1c54
#define RADEON_PP_TXFILTER_1		0x1c6c
#define RADEON_PP_TXFILTER_2		0x1c84

#define RADEON_RB2D_DSTCACHE_CTLSTAT	0x342c
#	define RADEON_RB2D_DC_FLUSH		(3 << 0)
#	define RADEON_RB2D_DC_FREE		(3 << 2)
#	define RADEON_RB2D_DC_FLUSH_ALL		0xf
#	define RADEON_RB2D_DC_BUSY		(1 << 31)
#define RADEON_RB3D_CNTL		0x1c3c
#	define RADEON_ALPHA_BLEND_ENABLE	(1 << 0)
#	define RADEON_PLANE_MASK_ENABLE		(1 << 1)
#	define RADEON_DITHER_ENABLE		(1 << 2)
#	define RADEON_ROUND_ENABLE		(1 << 3)
#	define RADEON_SCALE_DITHER_ENABLE	(1 << 4)
#	define RADEON_DITHER_INIT		(1 << 5)
#	define RADEON_ROP_ENABLE		(1 << 6)
#	define RADEON_STENCIL_ENABLE		(1 << 7)
#	define RADEON_Z_ENABLE			(1 << 8)
#define RADEON_RB3D_DEPTHOFFSET		0x1c24
#define RADEON_RB3D_DEPTHPITCH		0x1c28
#define RADEON_RB3D_PLANEMASK		0x1d84
#define RADEON_RB3D_STENCILREFMASK	0x1d7c
#define RADEON_RB3D_ZCACHE_MODE		0x3250
#define RADEON_RB3D_ZCACHE_CTLSTAT	0x3254
#	define RADEON_RB3D_ZC_FLUSH		(1 << 0)
#	define RADEON_RB3D_ZC_FREE		(1 << 2)
#	define RADEON_RB3D_ZC_FLUSH_ALL		0x5
#	define RADEON_RB3D_ZC_BUSY		(1 << 31)
#define RADEON_RB3D_ZSTENCILCNTL	0x1c2c
#	define RADEON_Z_TEST_MASK		(7 << 4)
#	define RADEON_Z_TEST_ALWAYS		(7 << 4)
#	define RADEON_STENCIL_TEST_ALWAYS	(7 << 12)
#	define RADEON_STENCIL_S_FAIL_REPLACE	(2 << 16)
#	define RADEON_STENCIL_ZPASS_REPLACE	(2 << 20)
#	define RADEON_STENCIL_ZFAIL_REPLACE	(2 << 24)
#	define RADEON_Z_WRITE_ENABLE		(1 << 30)
#define RADEON_RBBM_SOFT_RESET		0x00f0
#	define RADEON_SOFT_RESET_CP		(1 <<  0)
#	define RADEON_SOFT_RESET_HI		(1 <<  1)
#	define RADEON_SOFT_RESET_SE		(1 <<  2)
#	define RADEON_SOFT_RESET_RE		(1 <<  3)
#	define RADEON_SOFT_RESET_PP		(1 <<  4)
#	define RADEON_SOFT_RESET_E2		(1 <<  5)
#	define RADEON_SOFT_RESET_RB		(1 <<  6)
#	define RADEON_SOFT_RESET_HDP		(1 <<  7)
#define RADEON_RBBM_STATUS		0x0e40
#	define RADEON_RBBM_FIFOCNT_MASK		0x007f
#	define RADEON_RBBM_ACTIVE		(1 << 31)
#define RADEON_RE_LINE_PATTERN		0x1cd0
#define RADEON_RE_MISC			0x26c4
#define RADEON_RE_TOP_LEFT		0x26c0
#define RADEON_RE_WIDTH_HEIGHT		0x1c44
#define RADEON_RE_STIPPLE_ADDR		0x1cc8
#define RADEON_RE_STIPPLE_DATA		0x1ccc

#define RADEON_SCISSOR_TL_0		0x1cd8
#define RADEON_SCISSOR_BR_0		0x1cdc
#define RADEON_SCISSOR_TL_1		0x1ce0
#define RADEON_SCISSOR_BR_1		0x1ce4
#define RADEON_SCISSOR_TL_2		0x1ce8
#define RADEON_SCISSOR_BR_2		0x1cec
#define RADEON_SE_COORD_FMT		0x1c50
#define RADEON_SE_CNTL			0x1c4c
#	define RADEON_FFACE_CULL_CW		(0 << 0)
#	define RADEON_BFACE_SOLID		(3 << 1)
#	define RADEON_FFACE_SOLID		(3 << 3)
#	define RADEON_FLAT_SHADE_VTX_LAST	(3 << 6)
#	define RADEON_DIFFUSE_SHADE_FLAT	(1 << 8)
#	define RADEON_DIFFUSE_SHADE_GOURAUD	(2 << 8)
#	define RADEON_ALPHA_SHADE_FLAT		(1 << 10)
#	define RADEON_ALPHA_SHADE_GOURAUD	(2 << 10)
#	define RADEON_SPECULAR_SHADE_FLAT	(1 << 12)
#	define RADEON_SPECULAR_SHADE_GOURAUD	(2 << 12)
#	define RADEON_FOG_SHADE_FLAT		(1 << 14)
#	define RADEON_FOG_SHADE_GOURAUD		(2 << 14)
#	define RADEON_VPORT_XY_XFORM_ENABLE	(1 << 24)
#	define RADEON_VPORT_Z_XFORM_ENABLE	(1 << 25)
#	define RADEON_VTX_PIX_CENTER_OGL	(1 << 27)
#	define RADEON_ROUND_MODE_TRUNC		(0 << 28)
#	define RADEON_ROUND_PREC_8TH_PIX	(1 << 30)
#define RADEON_SE_CNTL_STATUS		0x2140
#define RADEON_SE_LINE_WIDTH		0x1db8
#define RADEON_SE_VPORT_XSCALE		0x1d98
#define RADEON_SE_ZBIAS_FACTOR		0x1db0
#define RADEON_SE_TCL_MATERIAL_EMMISSIVE_RED 0x2210
#define RADEON_SE_TCL_OUTPUT_VTX_FMT         0x2254
#define RADEON_SE_TCL_VECTOR_INDX_REG        0x2200
#       define RADEON_VEC_INDX_OCTWORD_STRIDE_SHIFT  16
#       define RADEON_VEC_INDX_DWORD_COUNT_SHIFT     28
#define RADEON_SE_TCL_VECTOR_DATA_REG       0x2204
#define RADEON_SE_TCL_SCALAR_INDX_REG       0x2208
#       define RADEON_SCAL_INDX_DWORD_STRIDE_SHIFT  16
#define RADEON_SE_TCL_SCALAR_DATA_REG       0x220C
#define RADEON_SURFACE_ACCESS_FLAGS	0x0bf8
#define RADEON_SURFACE_ACCESS_CLR	0x0bfc
#define RADEON_SURFACE_CNTL		0x0b00
#	define RADEON_SURF_TRANSLATION_DIS	(1 << 8)
#	define RADEON_NONSURF_AP0_SWP_MASK	(3 << 20)
#	define RADEON_NONSURF_AP0_SWP_LITTLE	(0 << 20)
#	define RADEON_NONSURF_AP0_SWP_BIG16	(1 << 20)
#	define RADEON_NONSURF_AP0_SWP_BIG32	(2 << 20)
#	define RADEON_NONSURF_AP1_SWP_MASK	(3 << 22)
#	define RADEON_NONSURF_AP1_SWP_LITTLE	(0 << 22)
#	define RADEON_NONSURF_AP1_SWP_BIG16	(1 << 22)
#	define RADEON_NONSURF_AP1_SWP_BIG32	(2 << 22)
#define RADEON_SURFACE0_INFO		0x0b0c
#	define RADEON_SURF_PITCHSEL_MASK	(0x1ff << 0)
#	define RADEON_SURF_TILE_MODE_MASK	(3 << 16)
#	define RADEON_SURF_TILE_MODE_MACRO	(0 << 16)
#	define RADEON_SURF_TILE_MODE_MICRO	(1 << 16)
#	define RADEON_SURF_TILE_MODE_32BIT_Z	(2 << 16)
#	define RADEON_SURF_TILE_MODE_16BIT_Z	(3 << 16)
#define RADEON_SURFACE0_LOWER_BOUND	0x0b04
#define RADEON_SURFACE0_UPPER_BOUND	0x0b08
#define RADEON_SURFACE1_INFO		0x0b1c
#define RADEON_SURFACE1_LOWER_BOUND	0x0b14
#define RADEON_SURFACE1_UPPER_BOUND	0x0b18
#define RADEON_SURFACE2_INFO		0x0b2c
#define RADEON_SURFACE2_LOWER_BOUND	0x0b24
#define RADEON_SURFACE2_UPPER_BOUND	0x0b28
#define RADEON_SURFACE3_INFO		0x0b3c
#define RADEON_SURFACE3_LOWER_BOUND	0x0b34
#define RADEON_SURFACE3_UPPER_BOUND	0x0b38
#define RADEON_SURFACE4_INFO		0x0b4c
#define RADEON_SURFACE4_LOWER_BOUND	0x0b44
#define RADEON_SURFACE4_UPPER_BOUND	0x0b48
#define RADEON_SURFACE5_INFO		0x0b5c
#define RADEON_SURFACE5_LOWER_BOUND	0x0b54
#define RADEON_SURFACE5_UPPER_BOUND	0x0b58
#define RADEON_SURFACE6_INFO		0x0b6c
#define RADEON_SURFACE6_LOWER_BOUND	0x0b64
#define RADEON_SURFACE6_UPPER_BOUND	0x0b68
#define RADEON_SURFACE7_INFO		0x0b7c
#define RADEON_SURFACE7_LOWER_BOUND	0x0b74
#define RADEON_SURFACE7_UPPER_BOUND	0x0b78
#define RADEON_SW_SEMAPHORE		0x013c

#define RADEON_WAIT_UNTIL		0x1720
#	define RADEON_WAIT_CRTC_PFLIP		(1 << 0)
#	define RADEON_WAIT_2D_IDLECLEAN		(1 << 16)
#	define RADEON_WAIT_3D_IDLECLEAN		(1 << 17)
#	define RADEON_WAIT_HOST_IDLECLEAN	(1 << 18)

#define RADEON_RB3D_ZMASKOFFSET		0x1c34
#define RADEON_RB3D_ZSTENCILCNTL	0x1c2c
#	define RADEON_DEPTH_FORMAT_16BIT_INT_Z	(0 << 0)
#	define RADEON_DEPTH_FORMAT_24BIT_INT_Z	(2 << 0)


/* CP registers */
#define RADEON_CP_ME_RAM_ADDR		0x07d4
#define RADEON_CP_ME_RAM_RADDR		0x07d8
#define RADEON_CP_ME_RAM_DATAH		0x07dc
#define RADEON_CP_ME_RAM_DATAL		0x07e0

#define RADEON_CP_RB_BASE		0x0700
#define RADEON_CP_RB_CNTL		0x0704
#	define RADEON_BUF_SWAP_32BIT		(2 << 16)
#define RADEON_CP_RB_RPTR_ADDR		0x070c
#define RADEON_CP_RB_RPTR		0x0710
#define RADEON_CP_RB_WPTR		0x0714

#define RADEON_CP_RB_WPTR_DELAY		0x0718
#	define RADEON_PRE_WRITE_TIMER_SHIFT	0
#	define RADEON_PRE_WRITE_LIMIT_SHIFT	23

#define RADEON_CP_IB_BASE		0x0738

#define RADEON_CP_CSQ_CNTL		0x0740
#	define RADEON_CSQ_CNT_PRIMARY_MASK	(0xff << 0)
#	define RADEON_CSQ_PRIDIS_INDDIS		(0 << 28)
#	define RADEON_CSQ_PRIPIO_INDDIS		(1 << 28)
#	define RADEON_CSQ_PRIBM_INDDIS		(2 << 28)
#	define RADEON_CSQ_PRIPIO_INDBM		(3 << 28)
#	define RADEON_CSQ_PRIBM_INDBM		(4 << 28)
#	define RADEON_CSQ_PRIPIO_INDPIO		(15 << 28)

#define RADEON_AIC_CNTL			0x01d0
#	define RADEON_PCIGART_TRANSLATE_EN	(1 << 0)
#define RADEON_AIC_STAT			0x01d4
#define RADEON_AIC_PT_BASE		0x01d8
#define RADEON_AIC_LO_ADDR		0x01dc
#define RADEON_AIC_HI_ADDR		0x01e0
#define RADEON_AIC_TLB_ADDR		0x01e4
#define RADEON_AIC_TLB_DATA		0x01e8

/* CP command packets */
#define RADEON_CP_PACKET0		0x00000000
#	define RADEON_ONE_REG_WR		(1 << 15)
#define RADEON_CP_PACKET1		0x40000000
#define RADEON_CP_PACKET2		0x80000000
#define RADEON_CP_PACKET3		0xC0000000
#	define RADEON_3D_RNDR_GEN_INDX_PRIM	0x00002300
#	define RADEON_WAIT_FOR_IDLE		0x00002600
#	define RADEON_3D_DRAW_VBUF		0x00002800
#	define RADEON_3D_DRAW_IMMD		0x00002900
#	define RADEON_3D_DRAW_INDX		0x00002A00
#	define RADEON_3D_LOAD_VBPNTR		0x00002F00
#	define RADEON_CNTL_HOSTDATA_BLT		0x00009400
#	define RADEON_CNTL_PAINT_MULTI		0x00009A00
#	define RADEON_CNTL_BITBLT_MULTI		0x00009B00
#	define RADEON_CNTL_SET_SCISSORS		0xC0001E00

#define RADEON_CP_PACKET_MASK		0xC0000000
#define RADEON_CP_PACKET_COUNT_MASK	0x3fff0000
#define RADEON_CP_PACKET0_REG_MASK	0x000007ff
#define RADEON_CP_PACKET1_REG0_MASK	0x000007ff
#define RADEON_CP_PACKET1_REG1_MASK	0x003ff800

#define RADEON_VTX_Z_PRESENT			(1 << 31)
#define RADEON_VTX_PKCOLOR_PRESENT		(1 << 3)

#define RADEON_PRIM_TYPE_NONE			(0 << 0)
#define RADEON_PRIM_TYPE_POINT			(1 << 0)
#define RADEON_PRIM_TYPE_LINE			(2 << 0)
#define RADEON_PRIM_TYPE_LINE_STRIP		(3 << 0)
#define RADEON_PRIM_TYPE_TRI_LIST		(4 << 0)
#define RADEON_PRIM_TYPE_TRI_FAN		(5 << 0)
#define RADEON_PRIM_TYPE_TRI_STRIP		(6 << 0)
#define RADEON_PRIM_TYPE_TRI_TYPE2		(7 << 0)
#define RADEON_PRIM_TYPE_RECT_LIST		(8 << 0)
#define RADEON_PRIM_TYPE_3VRT_POINT_LIST	(9 << 0)
#define RADEON_PRIM_TYPE_3VRT_LINE_LIST		(10 << 0)
#define RADEON_PRIM_TYPE_MASK                   0xf
#define RADEON_PRIM_WALK_IND			(1 << 4)
#define RADEON_PRIM_WALK_LIST			(2 << 4)
#define RADEON_PRIM_WALK_RING			(3 << 4)
#define RADEON_COLOR_ORDER_BGRA			(0 << 6)
#define RADEON_COLOR_ORDER_RGBA			(1 << 6)
#define RADEON_MAOS_ENABLE			(1 << 7)
#define RADEON_VTX_FMT_R128_MODE		(0 << 8)
#define RADEON_VTX_FMT_RADEON_MODE		(1 << 8)
#define RADEON_NUM_VERTICES_SHIFT		16

#define RADEON_COLOR_FORMAT_CI8		2
#define RADEON_COLOR_FORMAT_ARGB1555	3
#define RADEON_COLOR_FORMAT_RGB565	4
#define RADEON_COLOR_FORMAT_ARGB8888	6
#define RADEON_COLOR_FORMAT_RGB332	7
#define RADEON_COLOR_FORMAT_RGB8	9
#define RADEON_COLOR_FORMAT_ARGB4444	15

#define RADEON_TXFORMAT_I8		0
#define RADEON_TXFORMAT_AI88		1
#define RADEON_TXFORMAT_RGB332		2
#define RADEON_TXFORMAT_ARGB1555	3
#define RADEON_TXFORMAT_RGB565		4
#define RADEON_TXFORMAT_ARGB4444	5
#define RADEON_TXFORMAT_ARGB8888	6
#define RADEON_TXFORMAT_RGBA8888	7

#define R200_PP_TXCBLEND_0                0x2f00
#define R200_PP_TXCBLEND_1                0x2f10
#define R200_PP_TXCBLEND_2                0x2f20
#define R200_PP_TXCBLEND_3                0x2f30
#define R200_PP_TXCBLEND_4                0x2f40
#define R200_PP_TXCBLEND_5                0x2f50
#define R200_PP_TXCBLEND_6                0x2f60
#define R200_PP_TXCBLEND_7                0x2f70
#define R200_SE_TCL_LIGHT_MODEL_CTL_0     0x2268 
#define R200_PP_TFACTOR_0                 0x2ee0
#define R200_SE_VTX_FMT_0                 0x2088
#define R200_SE_VAP_CNTL                  0x2080
#define R200_SE_TCL_MATRIX_SEL_0          0x2230
#define R200_SE_TCL_TEX_PROC_CTL_2        0x22a8 
#define R200_SE_TCL_UCP_VERT_BLEND_CTL    0x22c0 
#define R200_PP_TXFILTER_5                0x2ca0 
#define R200_PP_TXFILTER_4                0x2c80 
#define R200_PP_TXFILTER_3                0x2c60 
#define R200_PP_TXFILTER_2                0x2c40 
#define R200_PP_TXFILTER_1                0x2c20 
#define R200_PP_TXFILTER_0                0x2c00 
#define R200_PP_TXOFFSET_5                0x2d78
#define R200_PP_TXOFFSET_4                0x2d60
#define R200_PP_TXOFFSET_3                0x2d48
#define R200_PP_TXOFFSET_2                0x2d30
#define R200_PP_TXOFFSET_1                0x2d18
#define R200_PP_TXOFFSET_0                0x2d00
#define R200_RE_AUX_SCISSOR_CNTL          0x26f0
#define R200_SE_VTE_CNTL                  0x20b0
#define R200_SE_TCL_OUTPUT_VTX_COMP_SEL   0x2250
#define R200_PP_TAM_DEBUG3                0x2d9c
#define R200_PP_CNTL_X                    0x2cc4
#define R200_SE_VAP_CNTL_STATUS           0x2140
#define R200_RE_SCISSOR_TL_0              0x1cd8
#define R200_RE_SCISSOR_TL_1              0x1ce0
#define R200_RE_SCISSOR_TL_2              0x1ce8
#define R200_RB3D_DEPTHXY_OFFSET          0x1d60 
#define R200_RE_AUX_SCISSOR_CNTL          0x26f0
#define R200_SE_VTX_STATE_CNTL            0x2180
#define R200_RE_POINTSIZE                 0x2648
#define R200_SE_TCL_INPUT_VTX_VECTOR_ADDR_0 0x2254


#define SE_VAP_CNTL__TCL_ENA_MASK                          0x00000001
#define SE_VAP_CNTL__FORCE_W_TO_ONE_MASK                   0x00010000
#define SE_VAP_CNTL__VF_MAX_VTX_NUM__SHIFT                 0x00000012
#define SE_VTE_CNTL__VTX_XY_FMT_MASK                       0x00000100
#define SE_VTE_CNTL__VTX_Z_FMT_MASK                        0x00000200
#define SE_VTX_FMT_0__VTX_Z0_PRESENT_MASK                  0x00000001
#define SE_VTX_FMT_0__VTX_W0_PRESENT_MASK                  0x00000002
#define SE_VTX_FMT_0__VTX_COLOR_0_FMT__SHIFT               0x0000000b
#define R200_3D_DRAW_IMMD_2      0xC0003500
#define R200_SE_VTX_FMT_1                 0x208c
#define R200_RE_CNTL                      0x1c50 


/* Constants */
#define RADEON_MAX_USEC_TIMEOUT		100000	/* 100 ms */

#define RADEON_LAST_FRAME_REG		RADEON_SCRATCH_REG0
#define RADEON_LAST_DISPATCH_REG	RADEON_SCRATCH_REG1
#define RADEON_LAST_CLEAR_REG		RADEON_SCRATCH_REG2
#define RADEON_LAST_DISPATCH		1

#define RADEON_MAX_VB_AGE		0x7fffffff
#define RADEON_MAX_VB_VERTS		(0xffff)

#define RADEON_RING_HIGH_MARK		128


#define RADEON_BASE(reg)	((unsigned long)(dev_priv->mmio->handle))
#define RADEON_ADDR(reg)	(RADEON_BASE( reg ) + reg)

#define RADEON_READ(reg)	DRM_READ32(  (volatile u32 *) RADEON_ADDR(reg) )
#define RADEON_WRITE(reg,val)	DRM_WRITE32( (volatile u32 *) RADEON_ADDR(reg), (val) )

#define RADEON_READ8(reg)	DRM_READ8(  (volatile u8 *) RADEON_ADDR(reg) )
#define RADEON_WRITE8(reg,val)	DRM_WRITE8( (volatile u8 *) RADEON_ADDR(reg), (val) )

#define RADEON_WRITE_PLL( addr, val )					\
do {									\
	RADEON_WRITE8( RADEON_CLOCK_CNTL_INDEX,				\
		       ((addr) & 0x1f) | RADEON_PLL_WR_EN );		\
	RADEON_WRITE( RADEON_CLOCK_CNTL_DATA, (val) );			\
} while (0)

extern int RADEON_READ_PLL( drm_device_t *dev, int addr );


#define CP_PACKET0( reg, n )						\
	(RADEON_CP_PACKET0 | ((n) << 16) | ((reg) >> 2))
#define CP_PACKET0_TABLE( reg, n )					\
	(RADEON_CP_PACKET0 | RADEON_ONE_REG_WR | ((n) << 16) | ((reg) >> 2))
#define CP_PACKET1( reg0, reg1 )					\
	(RADEON_CP_PACKET1 | (((reg1) >> 2) << 15) | ((reg0) >> 2))
#define CP_PACKET2()							\
	(RADEON_CP_PACKET2)
#define CP_PACKET3( pkt, n )						\
	(RADEON_CP_PACKET3 | (pkt) | ((n) << 16))


/* ================================================================
 * Engine control helper macros
 */

#define RADEON_WAIT_UNTIL_2D_IDLE() do {				\
	OUT_RING( CP_PACKET0( RADEON_WAIT_UNTIL, 0 ) );			\
	OUT_RING( (RADEON_WAIT_2D_IDLECLEAN |				\
		   RADEON_WAIT_HOST_IDLECLEAN) );			\
} while (0)

#define RADEON_WAIT_UNTIL_3D_IDLE() do {				\
	OUT_RING( CP_PACKET0( RADEON_WAIT_UNTIL, 0 ) );			\
	OUT_RING( (RADEON_WAIT_3D_IDLECLEAN |				\
		   RADEON_WAIT_HOST_IDLECLEAN) );			\
} while (0)

#define RADEON_WAIT_UNTIL_IDLE() do {					\
	OUT_RING( CP_PACKET0( RADEON_WAIT_UNTIL, 0 ) );			\
	OUT_RING( (RADEON_WAIT_2D_IDLECLEAN |				\
		   RADEON_WAIT_3D_IDLECLEAN |				\
		   RADEON_WAIT_HOST_IDLECLEAN) );			\
} while (0)

#define RADEON_WAIT_UNTIL_PAGE_FLIPPED() do {				\
	OUT_RING( CP_PACKET0( RADEON_WAIT_UNTIL, 0 ) );			\
	OUT_RING( RADEON_WAIT_CRTC_PFLIP );				\
} while (0)

#define RADEON_FLUSH_CACHE() do {					\
	OUT_RING( CP_PACKET0( RADEON_RB2D_DSTCACHE_CTLSTAT, 0 ) );	\
	OUT_RING( RADEON_RB2D_DC_FLUSH );				\
} while (0)

#define RADEON_PURGE_CACHE() do {					\
	OUT_RING( CP_PACKET0( RADEON_RB2D_DSTCACHE_CTLSTAT, 0 ) );	\
	OUT_RING( RADEON_RB2D_DC_FLUSH_ALL );				\
} while (0)

#define RADEON_FLUSH_ZCACHE() do {					\
	OUT_RING( CP_PACKET0( RADEON_RB3D_ZCACHE_CTLSTAT, 0 ) );	\
	OUT_RING( RADEON_RB3D_ZC_FLUSH );				\
} while (0)

#define RADEON_PURGE_ZCACHE() do {					\
	OUT_RING( CP_PACKET0( RADEON_RB3D_ZCACHE_CTLSTAT, 0 ) );	\
	OUT_RING( RADEON_RB3D_ZC_FLUSH_ALL );				\
} while (0)


/* ================================================================
 * Misc helper macros
 */

#define LOCK_TEST_WITH_RETURN( dev )					\
do {									\
	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||		\
	     dev->lock.pid != DRM_CURRENTPID ) {			\
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );	\
		return DRM_ERR(EINVAL);				\
	}								\
} while (0)


/* Perfbox functionality only.  
 */
#define RING_SPACE_TEST_WITH_RETURN( dev_priv )				\
do {									\
	if (!(dev_priv->stats.boxes & RADEON_BOX_DMA_IDLE)) {		\
		u32 head = GET_RING_HEAD(&dev_priv->ring);		\
		if (head == dev_priv->ring.tail)			\
			dev_priv->stats.boxes |= RADEON_BOX_DMA_IDLE;	\
	}								\
} while (0)

#define VB_AGE_TEST_WITH_RETURN( dev_priv )				\
do {									\
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;		\
	if ( sarea_priv->last_dispatch >= RADEON_MAX_VB_AGE ) {		\
		int __ret = radeon_do_cp_idle( dev_priv );		\
		if ( __ret ) return __ret;				\
		sarea_priv->last_dispatch = 0;				\
		radeon_freelist_reset( dev );				\
	}								\
} while (0)

#define RADEON_DISPATCH_AGE( age ) do {					\
	OUT_RING( CP_PACKET0( RADEON_LAST_DISPATCH_REG, 0 ) );		\
	OUT_RING( age );						\
} while (0)

#define RADEON_FRAME_AGE( age ) do {					\
	OUT_RING( CP_PACKET0( RADEON_LAST_FRAME_REG, 0 ) );		\
	OUT_RING( age );						\
} while (0)

#define RADEON_CLEAR_AGE( age ) do {					\
	OUT_RING( CP_PACKET0( RADEON_LAST_CLEAR_REG, 0 ) );		\
	OUT_RING( age );						\
} while (0)


/* ================================================================
 * Ring control
 */

#if defined(__powerpc__)
#define radeon_flush_write_combine()	(void) GET_RING_HEAD( &dev_priv->ring )
#else
#define radeon_flush_write_combine()	DRM_WRITEMEMORYBARRIER()
#endif


#define RADEON_VERBOSE	0

#define RING_LOCALS	int write, _nr; unsigned int mask; u32 *ring;

#define BEGIN_RING( n ) do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "BEGIN_RING( %d ) in %s\n",			\
			   n, __FUNCTION__ );				\
	}								\
	if ( dev_priv->ring.space <= (n) * sizeof(u32) ) {		\
                COMMIT_RING();						\
		radeon_wait_ring( dev_priv, (n) * sizeof(u32) );	\
	}								\
	_nr = n; dev_priv->ring.space -= (n) * sizeof(u32);		\
	ring = dev_priv->ring.start;					\
	write = dev_priv->ring.tail;					\
	mask = dev_priv->ring.tail_mask;				\
} while (0)

#define ADVANCE_RING() do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "ADVANCE_RING() wr=0x%06x tail=0x%06x\n",	\
			  write, dev_priv->ring.tail );			\
	}								\
	if (((dev_priv->ring.tail + _nr) & mask) != write) {		\
		DRM_ERROR( 						\
			"ADVANCE_RING(): mismatch: nr: %x write: %x line: %d\n",	\
			((dev_priv->ring.tail + _nr) & mask),		\
			write, __LINE__);						\
	} else								\
		dev_priv->ring.tail = write;				\
} while (0)

#define COMMIT_RING() do {					    \
	RADEON_WRITE( RADEON_CP_RB_WPTR, dev_priv->ring.tail );		    \
} while (0)

#define OUT_RING( x ) do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "   OUT_RING( 0x%08x ) at 0x%x\n",		\
			   (unsigned int)(x), write );			\
	}								\
	ring[write++] = (x);						\
	write &= mask;							\
} while (0)

#define OUT_RING_REG( reg, val ) do {					\
	OUT_RING( CP_PACKET0( reg, 0 ) );				\
	OUT_RING( val );						\
} while (0)


#define OUT_RING_USER_TABLE( tab, sz ) do {			\
	int _size = (sz);					\
	int *_tab = (tab);					\
								\
	if (write + _size > mask) {				\
		int i = (mask+1) - write;			\
		if (DRM_COPY_FROM_USER_UNCHECKED( (int *)(ring+write),	\
				      _tab, i*4 ))		\
			return DRM_ERR(EFAULT);		\
		write = 0;					\
		_size -= i;					\
		_tab += i;					\
	}							\
								\
	if (_size && DRM_COPY_FROM_USER_UNCHECKED( (int *)(ring+write),	\
			               _tab, _size*4 ))		\
		return DRM_ERR(EFAULT);			\
								\
	write += _size;						\
	write &= mask;						\
} while (0)


#endif /* __RADEON_DRV_H__ */
