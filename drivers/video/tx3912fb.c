/*
 *  drivers/video/tx3912fb.c
 *
 *  Copyright (C) 1999 Harald Koerfgen
 *  Copyright (C) 2001 Steven Hill (sjhill@realitydiluted.com)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 *  Framebuffer for LCD controller in TMPR3912/05 and PR31700 processors
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/fb.h>
#include <video/fbcon.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/uaccess.h>
#include <asm/tx3912.h>
#include <video/tx3912.h>

/*
 * Frame buffer, palette and console structures
 */
static struct fb_info fb_info;
static u32 cfb8[16];
static struct display disp;

static struct fb_fix_screeninfo tx3912fb_fix __initdata = {
	id:		"tx3912fb",
#ifdef CONFIG_NINO_16MB
	smem_len:	(240 * 320),
#else
	smem_len:	((240 * 320)/2),
#endif
	type:		FB_TYPE_PACKED_PIXELS,
	visual:		FB_VISUAL_TRUECOLOR, 
	xpanstep:	1,
	ypanstep:	1,
	ywrapstep:	1,
	accel:		FB_ACCEL_NONE,
};

static struct fb_var_screeninfo tx3912fb_var = {
	xres:		240,
	yres:		320,
	xres_virtual:	240,
	yres_virtual:	320,
#ifdef CONFIG_NINO_16MB
	bits_per_pixel:	8,
	red:		{ 5, 3, 0 },	/* RGB 332 */
	green:		{ 2, 3, 0 },
	blue:		{ 0, 2, 0 },
#else
	bits_per_pixel:	4,
#endif
	activate:	FB_ACTIVATE_NOW,
	width:		-1,
	height:		-1,
	pixclock:	20000,
	left_margin:	64,
	right_margin:	64,
	upper_margin:	32,
	lower_margin:	32,
	hsync_len:	64,
	vsync_len:	2,
	vmode:		FB_VMODE_NONINTERLACED,
};

/*
 * Interface used by the world
 */
int tx3912fb_init(void);

static int tx3912fb_setcolreg(u_int regno, u_int red, u_int green,
			      u_int blue, u_int transp,
			      struct fb_info *info);

/*
 * Macros
 */
#define get_line_length(xres_virtual, bpp) \
                (u_long) (((int) xres_virtual * (int) bpp + 7) >> 3)

/*
 * Frame buffer operations structure used by console driver
 */
static struct fb_ops tx3912fb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	gen_get_fix,
	fb_get_var:	gen_get_var,
	fb_set_var:	gen_set_var,
	fb_get_cmap:	gen_get_cmap,
	fb_set_cmap:	gen_set_cmap,
	fb_setcolreg:	tx3912fb_setcolreg,
	fb_fillrect:	cfb_fillrect,
	fb_copyarea:	cfb_copyarea,
	fb_imageblit:	cfb_imageblit,
};

static int tx3912fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	/*
	 * Memory limit
	 */
	line_length =
	    get_line_length(var->xres_virtual, var->bits_per_pixel);
	if ((line_length * var->yres_virtual) > info->fix.smem_len)
		return -ENOMEM;

	return 0;
}

static int tx3912fb_set_par(struct fb_info *info)
{
	u_long tx3912fb_paddr = 0;

	/* Disable the video logic */
	outl(inl(TX3912_VIDEO_CTRL1) &
	     ~(TX3912_VIDEO_CTRL1_ENVID | TX3912_VIDEO_CTRL1_DISPON),
	     TX3912_VIDEO_CTRL1);
	udelay(200);

	/* Set start address for DMA transfer */
	outl(tx3912fb_paddr, TX3912_VIDEO_CTRL3);

	/* Set end address for DMA transfer */
	outl((tx3912fb_paddr + tx3912fb_fix.smem_len + 1), TX3912_VIDEO_CTRL4);

	/* Set the pixel depth */
	switch (info->var.bits_per_pixel) {
	case 1:
		/* Monochrome */
		outl(inl(TX3912_VIDEO_CTRL1) &
		     ~TX3912_VIDEO_CTRL1_BITSEL_MASK, TX3912_VIDEO_CTRL1);
		info->fix.visual = FB_VISUAL_MONO10;
		break;
	case 4:
		/* 4-bit gray */
		outl(inl(TX3912_VIDEO_CTRL1) &
		     ~TX3912_VIDEO_CTRL1_BITSEL_MASK, TX3912_VIDEO_CTRL1);
		outl(inl(TX3912_VIDEO_CTRL1) |
		     TX3912_VIDEO_CTRL1_BITSEL_4BIT_GRAY,
		     TX3912_VIDEO_CTRL1);
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	case 8:
		/* 8-bit color */
		outl(inl(TX3912_VIDEO_CTRL1) &
		     ~TX3912_VIDEO_CTRL1_BITSEL_MASK, TX3912_VIDEO_CTRL1);
		outl(inl(TX3912_VIDEO_CTRL1) |
		     TX3912_VIDEO_CTRL1_BITSEL_8BIT_COLOR,
		     TX3912_VIDEO_CTRL1);
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	case 2:
	default:
		/* 2-bit gray */
		outl(inl(TX3912_VIDEO_CTRL1) &
		     ~TX3912_VIDEO_CTRL1_BITSEL_MASK, TX3912_VIDEO_CTRL1);
		outl(inl(TX3912_VIDEO_CTRL1) |
		     TX3912_VIDEO_CTRL1_BITSEL_2BIT_GRAY,
		     TX3912_VIDEO_CTRL1);
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	}

	/* Enable the video clock */
	outl(inl(TX3912_CLK_CTRL) | TX3912_CLK_CTRL_ENVIDCLK,
	     TX3912_CLK_CTRL);

	/* Unfreeze video logic and enable DF toggle */
	outl(inl(TX3912_VIDEO_CTRL1) &
	     ~(TX3912_VIDEO_CTRL1_ENFREEZEFRAME |
	       TX3912_VIDEO_CTRL1_DFMODE)
	     , TX3912_VIDEO_CTRL1);
	udelay(200);

	/* Enable the video logic */
	outl(inl(TX3912_VIDEO_CTRL1) |
	     (TX3912_VIDEO_CTRL1_ENVID | TX3912_VIDEO_CTRL1_DISPON),
	     TX3912_VIDEO_CTRL1);

	info->fix.line_length = get_line_length(var->xres_virtual,
					    var->bits_per_pixel);
}

/*
 * Set a single color register
 */
static int tx3912fb_setcolreg(u_int regno, u_int red, u_int green,
			      u_int blue, u_int transp,
			      struct fb_info *info)
{
	if (regno > 255)
		return 1;

	if (regno < 16)
		((u32 *)(info->pseudo_palette))[regno] = ((red & 0xe000) >> 8)
		    | ((green & 0xe000) >> 11)
		    | ((blue & 0xc000) >> 14);
	return 0;
}

/*
 * Initialization of the framebuffer
 */
int __init tx3912fb_init(void)
{
	u_long tx3912fb_paddr = 0;

	/* Disable the video logic */
	outl(inl(TX3912_VIDEO_CTRL1) &
	     ~(TX3912_VIDEO_CTRL1_ENVID | TX3912_VIDEO_CTRL1_DISPON),
	     TX3912_VIDEO_CTRL1);
	udelay(200);

	/* Set start address for DMA transfer */
	outl(tx3912fb_paddr, TX3912_VIDEO_CTRL3);

	/* Set end address for DMA transfer */
	outl((tx3912fb_paddr + tx3912fb_fix.smem_len + 1), TX3912_VIDEO_CTRL4);

	/* Set the pixel depth */
	switch (tx3912fb_var.bits_per_pixel) {
	case 1:
		/* Monochrome */
		outl(inl(TX3912_VIDEO_CTRL1) &
		     ~TX3912_VIDEO_CTRL1_BITSEL_MASK, TX3912_VIDEO_CTRL1);
		tx3912fb_fix.visual = FB_VISUAL_MONO10;
		break;
	case 4:
		/* 4-bit gray */
		outl(inl(TX3912_VIDEO_CTRL1) &
		     ~TX3912_VIDEO_CTRL1_BITSEL_MASK, TX3912_VIDEO_CTRL1);
		outl(inl(TX3912_VIDEO_CTRL1) |
		     TX3912_VIDEO_CTRL1_BITSEL_4BIT_GRAY,
		     TX3912_VIDEO_CTRL1);
		tx3912fb_fix.visual = FB_VISUAL_TRUECOLOR;
		tx3912fb_fix.grayscale = 1;
		break;
	case 8:
		/* 8-bit color */
		outl(inl(TX3912_VIDEO_CTRL1) &
		     ~TX3912_VIDEO_CTRL1_BITSEL_MASK, TX3912_VIDEO_CTRL1);
		outl(inl(TX3912_VIDEO_CTRL1) |
		     TX3912_VIDEO_CTRL1_BITSEL_8BIT_COLOR,
		     TX3912_VIDEO_CTRL1);
		tx3912fb_fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	case 2:
	default:
		/* 2-bit gray */
		outl(inl(TX3912_VIDEO_CTRL1) &
		     ~TX3912_VIDEO_CTRL1_BITSEL_MASK, TX3912_VIDEO_CTRL1);
		outl(inl(TX3912_VIDEO_CTRL1) |
		     TX3912_VIDEO_CTRL1_BITSEL_2BIT_GRAY,
		     TX3912_VIDEO_CTRL1);
		tx3912fb_fix.visual = FB_VISUAL_PSEUDOCOLOR;
		tx3912fb_fix.grayscale = 1;
		break;
	}

	/* Enable the video clock */
	outl(inl(TX3912_CLK_CTRL) | TX3912_CLK_CTRL_ENVIDCLK,
		TX3912_CLK_CTRL);

	/* Unfreeze video logic and enable DF toggle */
	outl(inl(TX3912_VIDEO_CTRL1) &
		~(TX3912_VIDEO_CTRL1_ENFREEZEFRAME | TX3912_VIDEO_CTRL1_DFMODE),
		TX3912_VIDEO_CTRL1);
	udelay(200);

	/* Clear the framebuffer */
	memset((void *) tx3912fb_fix.smem_start, 0xff, tx3912fb_fix.smem_len);
	udelay(200);

	/* Enable the video logic */
	outl(inl(TX3912_VIDEO_CTRL1) |
		(TX3912_VIDEO_CTRL1_ENVID | TX3912_VIDEO_CTRL1_DISPON),
		TX3912_VIDEO_CTRL1);

	/*
	 * Memory limit
	 */
	tx3912fb_fix.line_length =
	    get_line_length(tx3912fb_var.xres_virtual, tx3912fb_var.bits_per_pixel);
	if ((tx3912fb_fix.line_length * tx3912fb_var.yres_virtual) > tx3912fb_fix.smem_len)
		return -ENOMEM;

	strcpy(fb_info.modename, tx3912fb_fix.id);
	fb_info.changevar = NULL;
	fb_info.node = NODEV;
	fb_info.currcon = -1;
	fb_info.fbops = &tx3912fb_ops;
	fb_info.var = tx3912fb_var;
	fb_info.fix = tx3912fb_fix;
	fb_info.pseudo_palette = pseudo_palette;
	fb_info.disp = &disp;
	fb_info.switch_con = gen_switch;
	fb_info.updatevar = gen_update_var;
	fb_info.flags = FBINFO_FLAG_DEFAULT;

	/* Clear the framebuffer */
	memset((void *) fb_info.fix.smem_start, 0xff, fb_info.fix.smem_len);
	udelay(200);

	fb_alloc_cmap(&info->cmap, size, 0);
	gen_set_disp(-1, &disp);	

	if (register_framebuffer(&fb_info) < 0)
		return -1;

	printk(KERN_INFO "fb%d: TX3912 frame buffer using %uKB.\n",
	       GET_FB_IDX(fb_info.node), (u_int) (fb_info.fix.smem_len >> 10));
	return 0;
}

int __init tx3912fb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ","))) {
		if (!strncmp(options, "bpp:", 4))	
			tx3912fb_var.bits_per_pixel = simple_strtoul(options+4, NULL, 0);
	}	
}

MODULE_LICENSE("GPL");
