/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 *  arch/ppc/platforms/pmac_feature.c
 *
 *  Copyright (C) 1996-2001 Paul Mackerras (paulus@cs.anu.edu.au)
 *                          Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *  
 *  TODO: 
 *  
 *   - Replace mdelay with some schedule loop if possible
 *   - Shorten some obfuscated delays on some routines (like modem
 *     power)
 *
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <asm/sections.h>
#include <asm/errno.h>
#include <asm/ohare.h>
#include <asm/heathrow.h>
#include <asm/keylargo.h>
#include <asm/uninorth.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/dbdma.h>

#undef DEBUG_FEATURE

#ifdef DEBUG_FEATURE
#define DBG(fmt,...) printk(KERN_DEBUG fmt)
#else
#define DBG(fmt,...)
#endif

/* Exported from arch/ppc/kernel/idle.c */
extern unsigned long powersave_nap;

/*
 * We use a single global lock to protect accesses. Each driver has
 * to take care of it's own locking
 */
static spinlock_t feature_lock  __pmacdata = SPIN_LOCK_UNLOCKED;

#define LOCK(flags)	spin_lock_irqsave(&feature_lock, flags);
#define UNLOCK(flags)	spin_unlock_irqrestore(&feature_lock, flags);

/*
 * Helper functions regarding the various flavors of mac-io
 */
 
#define MAX_MACIO_CHIPS		2

enum {
	macio_unknown = 0,
	macio_grand_central,
	macio_ohare,
	macio_ohareII,
	macio_heathrow,
	macio_gatwick,
	macio_paddington,
	macio_keylargo,
	macio_pangea
};

static const char* macio_names[] __pmacdata = 
{
	"Unknown",
	"Grand Central",
	"OHare",
	"OHareII",
	"Heathrow",
	"Gatwick",
	"Paddington",
	"Keylargo",
	"Pangea"
};

static struct macio_chip
{
	struct device_node*	of_node;
	int			type;
	int			rev;
	volatile u32*		base;
	unsigned long		flags;
} macio_chips[MAX_MACIO_CHIPS]  __pmacdata;

#define MACIO_FLAG_SCCA_ON	0x00000001
#define MACIO_FLAG_SCCB_ON	0x00000002
#define MACIO_FLAG_SCC_LOCKED	0x00000004
#define MACIO_FLAG_AIRPORT_ON	0x00000010
#define MACIO_FLAG_FW_SUPPORTED	0x00000020

static struct macio_chip* __pmac
macio_find(struct device_node* child, int type)
{
	while(child) {
		int	i;
	
		for (i=0; i < MAX_MACIO_CHIPS && macio_chips[i].of_node; i++)
			if (child == macio_chips[i].of_node &&
			    (!type || macio_chips[i].type == type))
				return &macio_chips[i];
		child = child->parent;
	}
	return NULL;
}

#define MACIO_FCR32(macio, r)	((macio)->base + ((r) >> 2))
#define MACIO_FCR8(macio, r)	(((volatile u8*)((macio)->base)) + (r))

#define MACIO_IN32(r)		(in_le32(MACIO_FCR32(macio,r)))
#define MACIO_OUT32(r,v)	(out_le32(MACIO_FCR32(macio,r), (v)))
#define MACIO_BIS(r,v)		(MACIO_OUT32((r), MACIO_IN32(r) | (v)))
#define MACIO_BIC(r,v)		(MACIO_OUT32((r), MACIO_IN32(r) & ~(v)))
#define MACIO_IN8(r)		(in_8(MACIO_FCR8(macio,r)))
#define MACIO_OUT8(r,v)		(out_8(MACIO_FCR8(macio,r), (v)))

/*
 * Uninorth reg. access. Note that Uni-N regs are big endian
 */
 
#define UN_REG(r)	(uninorth_base + ((r) >> 2))
#define UN_IN(r)	(in_be32(UN_REG(r)))
#define UN_OUT(r,v)	(out_be32(UN_REG(r), (v)))
#define UN_BIS(r,v)	(UN_OUT((r), UN_IN(r) | (v)))
#define UN_BIC(r,v)	(UN_OUT((r), UN_IN(r) & ~(v)))

static struct device_node* uninorth_node __pmacdata;
static u32* uninorth_base __pmacdata;
static u32 uninorth_rev __pmacdata;


/*
 * For each motherboard family, we have a table of functions pointers
 * that handle the various features.
 */

typedef int (*feature_call)(struct device_node* node, int param, int value);

struct feature_table_entry {
	unsigned int	selector;	
	feature_call	function;
};

struct pmac_mb_def
{
	const char*			model_string;
	const char*			model_name;
	int				model_id;
	struct feature_table_entry* 	features;
	unsigned long			board_flags;
};
static struct pmac_mb_def pmac_mb __pmacdata;

/*
 * Here are the chip specific feature functions
 */

static inline int __pmac
simple_feature_tweak(struct device_node* node, int type, int reg, u32 mask, int value)
{
	struct macio_chip*	macio;
	unsigned long		flags;
	
	macio = macio_find(node, type);
	if (!macio)
		return -ENODEV;
	LOCK(flags);
	if (value)
		MACIO_BIS(reg, mask);
	else
		MACIO_BIC(reg, mask);
	(void)MACIO_IN32(reg);
	UNLOCK(flags);

	return 0;
}

static int __pmac
generic_scc_enable(struct device_node* node, u32 enable_mask, u32 reset_mask,
	int param, int value)
{
	struct macio_chip*	macio;
	unsigned long		chan_mask;
	unsigned long		fcr;
	unsigned long		flags;
	
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	if (!strcmp(node->name, "ch-a"))
		chan_mask = MACIO_FLAG_SCCA_ON;
	else if (!strcmp(node->name, "ch-b"))
		chan_mask = MACIO_FLAG_SCCB_ON;
	else
		return -ENODEV;

	if (value) {
		LOCK(flags);
		fcr = MACIO_IN32(OHARE_FCR);
		/* Check if scc cell need enabling */
		if (!(fcr & OH_SCC_ENABLE)) {
			fcr |= enable_mask;
			MACIO_OUT32(OHARE_FCR, fcr);
			fcr |= reset_mask;
			MACIO_OUT32(OHARE_FCR, fcr);
			UNLOCK(flags);
			(void)MACIO_IN32(OHARE_FCR);
			mdelay(15);
			LOCK(flags);
			fcr &= ~reset_mask;
			MACIO_OUT32(OHARE_FCR, fcr);
		}
		if (chan_mask & MACIO_FLAG_SCCA_ON)
			fcr |= OH_SCCA_IO;
		if (chan_mask & MACIO_FLAG_SCCB_ON)
			fcr |= OH_SCCB_IO;
		MACIO_OUT32(OHARE_FCR, fcr);
		macio->flags |= chan_mask;
		UNLOCK(flags);
		if (param & PMAC_SCC_FLAG_XMON)
			macio->flags |= MACIO_FLAG_SCC_LOCKED;
	} else {
		if (macio->flags & MACIO_FLAG_SCC_LOCKED)
			return -EPERM;
		LOCK(flags);
		fcr = MACIO_IN32(OHARE_FCR);
		if (chan_mask & MACIO_FLAG_SCCA_ON)
			fcr &= ~OH_SCCA_IO;
		if (chan_mask & MACIO_FLAG_SCCB_ON)
			fcr &= ~OH_SCCB_IO;
		MACIO_OUT32(OHARE_FCR, fcr);
		if ((fcr & (OH_SCCA_IO | OH_SCCB_IO)) == 0) {
			fcr &= ~enable_mask;
			MACIO_OUT32(OHARE_FCR, fcr);
		}
		macio->flags &= ~(chan_mask);
		UNLOCK(flags);
		mdelay(10);
	}
	return 0;
}

static int __pmac
ohare_scc_enable(struct device_node* node, int param, int value)
{
	int rc;

#ifdef CONFIG_ADB_PMU
	if (value && (param & 0xfff) == PMAC_SCC_IRDA)
		pmu_enable_irled(1);
#endif /* CONFIG_ADB_PMU */		
	rc = generic_scc_enable(node, OH_SCC_ENABLE, OH_SCC_RESET, param, value);
#ifdef CONFIG_ADB_PMU
	if ((param & 0xfff) == PMAC_SCC_IRDA && (rc || !value))
		pmu_enable_irled(0);
#endif /* CONFIG_ADB_PMU */		
	return rc;
}

static int __pmac
ohare_floppy_enable(struct device_node* node, int param, int value)
{
	return simple_feature_tweak(node, macio_ohare,
		OHARE_FCR, OH_FLOPPY_ENABLE, value);
}

static int __pmac
ohare_mesh_enable(struct device_node* node, int param, int value)
{
	return simple_feature_tweak(node, macio_ohare,
		OHARE_FCR, OH_MESH_ENABLE, value);
}

static int __pmac
ohare_ide_enable(struct device_node* node, int param, int value)
{
	switch(param) {
	    case 0:
	    	/* For some reason, setting the bit in set_initial_features()
	    	 * doesn't stick. I'm still investigating... --BenH.
	    	 */
	    	if (value)
	    		simple_feature_tweak(node, macio_ohare,
				OHARE_FCR, OH_IOBUS_ENABLE, 1);
		return simple_feature_tweak(node, macio_ohare,
			OHARE_FCR, OH_IDE0_ENABLE, value);
	    case 1:
		return simple_feature_tweak(node, macio_ohare,
			OHARE_FCR, OH_BAY_IDE_ENABLE, value);
	    default:
	    	return -ENODEV;
	}
}

static int __pmac
ohare_ide_reset(struct device_node* node, int param, int value)
{
	switch(param) {
	    case 0:
		return simple_feature_tweak(node, macio_ohare,
			OHARE_FCR, OH_IDE0_RESET_N, !value);
	    case 1:
		return simple_feature_tweak(node, macio_ohare,
			OHARE_FCR, OH_IDE1_RESET_N, !value);
	    default:
	    	return -ENODEV;
	}
}

static int __pmac
ohare_sleep_state(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio = &macio_chips[0];

	if ((pmac_mb.board_flags & PMAC_MB_CAN_SLEEP) == 0)
		return -EPERM;
	if (value) {
		MACIO_BIC(OHARE_FCR, OH_IOBUS_ENABLE);
	} else {
		MACIO_BIS(OHARE_FCR, OH_IOBUS_ENABLE);
	}
	
	return 0;
}

static int __pmac
heathrow_scc_enable(struct device_node* node, int param, int value)
{
	int rc;
	
#ifdef CONFIG_ADB_PMU
	if (value && param == PMAC_SCC_IRDA)
		pmu_enable_irled(1);
#endif /* CONFIG_ADB_PMU */		
	/* Fixme: It's possible that wallstreet (heathrow) is different
	 * than other paddington machines. I still have to figure that
	 * out exactly, for now, the paddington values are used
	 */
	rc = generic_scc_enable(node, HRW_SCC_ENABLE, PADD_RESET_SCC, param, value);
#ifdef CONFIG_ADB_PMU
	if (param == PMAC_SCC_IRDA && (rc || !value))
		pmu_enable_irled(0);
#endif /* CONFIG_ADB_PMU */		
	return rc;
}

static int __pmac
heathrow_modem_enable(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio;
	u8			gpio;
	unsigned long		flags;
	
	macio = macio_find(node, macio_unknown);
	if (!macio)
		return -ENODEV;
	gpio = MACIO_IN8(HRW_GPIO_MODEM_RESET) & ~1;
	if (!value) {
		LOCK(flags);
		MACIO_OUT8(HRW_GPIO_MODEM_RESET, gpio);
		UNLOCK(flags);
		(void)MACIO_IN8(HRW_GPIO_MODEM_RESET);
		mdelay(250);
	}
	if (pmac_mb.model_id != PMAC_TYPE_YOSEMITE &&
	    pmac_mb.model_id != PMAC_TYPE_YIKES) {
	    	LOCK(flags);
	    	/* We use the paddington values as they seem to work properly
	    	 * on the wallstreet (heathrow) as well. I can't tell why we
	    	 * had to flip them on older feature.c, the fact is that new
	    	 * code uses the paddington values which are also the ones used
	    	 * in Darwin, and that works on wallstreet !
	    	 */
	    	if (value)
	    		MACIO_BIC(HEATHROW_FCR, PADD_MODEM_POWER_N);
	    	else
	    		MACIO_BIS(HEATHROW_FCR, PADD_MODEM_POWER_N);
	    	UNLOCK(flags);
	    	(void)MACIO_IN32(HEATHROW_FCR);
		mdelay(250);
	}
	if (value) {
		LOCK(flags);
		MACIO_OUT8(HRW_GPIO_MODEM_RESET, gpio | 1);
		(void)MACIO_IN8(HRW_GPIO_MODEM_RESET);
	    	UNLOCK(flags);
		mdelay(250);
		LOCK(flags);
		MACIO_OUT8(HRW_GPIO_MODEM_RESET, gpio);
		(void)MACIO_IN8(HRW_GPIO_MODEM_RESET);
	    	UNLOCK(flags);
		mdelay(250);
		LOCK(flags);
		MACIO_OUT8(HRW_GPIO_MODEM_RESET, gpio | 1);
		(void)MACIO_IN8(HRW_GPIO_MODEM_RESET);
	    	UNLOCK(flags);
	}
	return 0;
}

static int __pmac
heathrow_floppy_enable(struct device_node* node, int param, int value)
{
	return simple_feature_tweak(node, macio_unknown,
		HEATHROW_FCR,
		HRW_SWIM_ENABLE|HRW_BAY_FLOPPY_ENABLE,
		value);
}

static int __pmac
heathrow_mesh_enable(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio;
	unsigned long		flags;
	
	macio = macio_find(node, macio_unknown);
	if (!macio)
		return -ENODEV;
	LOCK(flags);
	/* Set clear mesh cell enable */
	if (value)
		MACIO_BIS(HEATHROW_FCR, HRW_MESH_ENABLE);
	else
		MACIO_BIC(HEATHROW_FCR, HRW_MESH_ENABLE);
	(void)MACIO_IN32(HEATHROW_FCR);
	udelay(10);
	/* Set/Clear termination power (todo: test ! the bit value
	 * used by Darwin doesn't seem to match what we used so
	 * far. If you experience problems, turn #if 1 into #if 0
	 * and tell me about it --BenH.
	 */
#if 1
	if (value)
		MACIO_BIC(HEATHROW_MBCR, 0x00000004);
	else
		MACIO_BIS(HEATHROW_MBCR, 0x00000004);
#else
	if (value)
		MACIO_BIC(HEATHROW_MBCR, 0x00040000);
	else
		MACIO_BIS(HEATHROW_MBCR, 0x00040000);
#endif		
	(void)MACIO_IN32(HEATHROW_MBCR);
	udelay(10);
	UNLOCK(flags);

	return 0;
}

static int __pmac
heathrow_ide_enable(struct device_node* node, int param, int value)
{
	switch(param) {
	    case 0:
		return simple_feature_tweak(node, macio_unknown,
			HEATHROW_FCR, HRW_IDE0_ENABLE, value);
	    case 1:
		return simple_feature_tweak(node, macio_unknown,
			HEATHROW_FCR, HRW_BAY_IDE_ENABLE, value);
	    default:
	    	return -ENODEV;
	}
}

static int __pmac
heathrow_ide_reset(struct device_node* node, int param, int value)
{
	switch(param) {
	    case 0:
		return simple_feature_tweak(node, macio_unknown,
			HEATHROW_FCR, HRW_IDE0_RESET_N, !value);
	    case 1:
		return simple_feature_tweak(node, macio_unknown,
			HEATHROW_FCR, HRW_IDE1_RESET_N, !value);
	    default:
	    	return -ENODEV;
	}
}

static int __pmac
heathrow_bmac_enable(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio;
	unsigned long		flags;
	
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	if (value) {
		LOCK(flags);
		MACIO_BIS(HEATHROW_FCR, HRW_BMAC_IO_ENABLE);
		MACIO_BIS(HEATHROW_FCR, HRW_BMAC_RESET);
		UNLOCK(flags);
		(void)MACIO_IN32(HEATHROW_FCR);
		mdelay(10);
		LOCK(flags);
		MACIO_BIC(HEATHROW_FCR, HRW_BMAC_RESET);
		UNLOCK(flags);
		(void)MACIO_IN32(HEATHROW_FCR);
		mdelay(10);
	} else {
		LOCK(flags);
		MACIO_BIC(HEATHROW_FCR, HRW_BMAC_IO_ENABLE);
		UNLOCK(flags);
	}
	return 0;
}

static int __pmac
heathrow_sound_enable(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio;
	unsigned long		flags;

	/* B&W G3 and Yikes don't support that properly (the
	 * sound appear to never come back after beeing shut down).
	 */
	if (pmac_mb.model_id == PMAC_TYPE_YOSEMITE ||
	    pmac_mb.model_id == PMAC_TYPE_YIKES)
		return 0;
	
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	if (value) {
		LOCK(flags);
		MACIO_BIS(HEATHROW_FCR, HRW_SOUND_CLK_ENABLE);
		MACIO_BIC(HEATHROW_FCR, HRW_SOUND_POWER_N);
		UNLOCK(flags);
		(void)MACIO_IN32(HEATHROW_FCR);
	} else {
		LOCK(flags);
		MACIO_BIS(HEATHROW_FCR, HRW_SOUND_POWER_N);
		MACIO_BIC(HEATHROW_FCR, HRW_SOUND_CLK_ENABLE);
		UNLOCK(flags);
	}
	return 0;
}

static u32 save_fcr[5] __pmacdata;
static u32 save_mbcr __pmacdata;
static u32 save_gpio_levels[2] __pmacdata;
static u8 save_gpio_extint[KEYLARGO_GPIO_EXTINT_CNT] __pmacdata;
static u8 save_gpio_normal[KEYLARGO_GPIO_CNT] __pmacdata;
static u32 save_unin_clock_ctl __pmacdata;
static struct dbdma_regs save_dbdma[13] __pmacdata;
static struct dbdma_regs save_alt_dbdma[13] __pmacdata;

static void __pmac
dbdma_save(struct macio_chip* macio, struct dbdma_regs* save)
{
	int i;
	
	/* Save state & config of DBDMA channels */
	for (i=0; i<13; i++) {
		volatile struct dbdma_regs* chan = (volatile struct dbdma_regs*)
			(macio->base + ((0x8000+i*0x100)>>2));
		save[i].cmdptr_hi = in_le32(&chan->cmdptr_hi);
		save[i].cmdptr = in_le32(&chan->cmdptr);
		save[i].intr_sel = in_le32(&chan->intr_sel);
		save[i].br_sel = in_le32(&chan->br_sel);
		save[i].wait_sel = in_le32(&chan->wait_sel);
	}
}

static void __pmac
dbdma_restore(struct macio_chip* macio, struct dbdma_regs* save)
{
	int i;
	
	/* Save state & config of DBDMA channels */
	for (i=0; i<13; i++) {
		volatile struct dbdma_regs* chan = (volatile struct dbdma_regs*)
			(macio->base + ((0x8000+i*0x100)>>2));
		out_le32(&chan->control, (ACTIVE|DEAD|WAKE|FLUSH|PAUSE|RUN)<<16);
		while (in_le32(&chan->status) & ACTIVE)
			mb();
		out_le32(&chan->cmdptr_hi, save[i].cmdptr_hi);
		out_le32(&chan->cmdptr, save[i].cmdptr);
		out_le32(&chan->intr_sel, save[i].intr_sel);
		out_le32(&chan->br_sel, save[i].br_sel);
		out_le32(&chan->wait_sel, save[i].wait_sel);
	}
}

static void __pmac
heathrow_sleep(struct macio_chip* macio, int secondary)
{
	if (secondary) {
		dbdma_save(macio, save_alt_dbdma);
		save_fcr[2] = MACIO_IN32(0x38);
		save_fcr[3] = MACIO_IN32(0x3c);
	} else {
		dbdma_save(macio, save_dbdma);
		save_fcr[0] = MACIO_IN32(0x38);
		save_fcr[1] = MACIO_IN32(0x3c);
		save_mbcr = MACIO_IN32(0x34);
		/* Make sure sound is shut down */
		MACIO_BIS(HEATHROW_FCR, HRW_SOUND_POWER_N);
		MACIO_BIC(HEATHROW_FCR, HRW_SOUND_CLK_ENABLE);
		/* This seems to be necessary as well or the fan
		 * keeps coming up and battery drains fast */
		MACIO_BIC(HEATHROW_FCR, HRW_IOBUS_ENABLE);
	}
	/* Make sure modem is shut down */
	MACIO_OUT8(HRW_GPIO_MODEM_RESET,
		MACIO_IN8(HRW_GPIO_MODEM_RESET) & ~1);
	MACIO_BIS(HEATHROW_FCR, PADD_MODEM_POWER_N);
	MACIO_BIC(HEATHROW_FCR, OH_SCCA_IO|OH_SCCB_IO|HRW_SCC_ENABLE);

	/* Let things settle */
	(void)MACIO_IN32(HEATHROW_FCR);
	mdelay(1);
}

static void __pmac
heathrow_wakeup(struct macio_chip* macio, int secondary)
{
	if (secondary) {
		MACIO_OUT32(0x38, save_fcr[2]);
		(void)MACIO_IN32(0x38);
		mdelay(1);
		MACIO_OUT32(0x3c, save_fcr[3]);
		(void)MACIO_IN32(0x38);
		mdelay(10);
		dbdma_restore(macio, save_alt_dbdma);
	} else {
		MACIO_OUT32(0x38, save_fcr[0] | HRW_IOBUS_ENABLE);
		(void)MACIO_IN32(0x38);
		mdelay(1);
		MACIO_OUT32(0x3c, save_fcr[1]);
		(void)MACIO_IN32(0x38);
		mdelay(1);
		MACIO_OUT32(0x34, save_mbcr);
		(void)MACIO_IN32(0x38);
		mdelay(10);
		dbdma_restore(macio, save_dbdma);
	}
}

static int __pmac
heathrow_sleep_state(struct device_node* node, int param, int value)
{
	if ((pmac_mb.board_flags & PMAC_MB_CAN_SLEEP) == 0)
		return -EPERM;
	if (value == 1) {
		if (macio_chips[1].type == macio_gatwick)
			heathrow_sleep(&macio_chips[0], 1);
		heathrow_sleep(&macio_chips[0], 0);
	} else if (value == 0) {
		heathrow_wakeup(&macio_chips[0], 0);
		if (macio_chips[1].type == macio_gatwick)
			heathrow_wakeup(&macio_chips[0], 1);
	}
	return 0;
}

static int __pmac
core99_scc_enable(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio;
	unsigned long		flags;
	unsigned long		chan_mask;
	u32			fcr;
	
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	if (!strcmp(node->name, "ch-a"))
		chan_mask = MACIO_FLAG_SCCA_ON;
	else if (!strcmp(node->name, "ch-b"))
		chan_mask = MACIO_FLAG_SCCB_ON;
	else
		return -ENODEV;

	if (value) {
		int need_reset_scc = 0;
		int need_reset_irda = 0;
		
		LOCK(flags);
		fcr = MACIO_IN32(KEYLARGO_FCR0);
		/* Check if scc cell need enabling */
		if (!(fcr & KL0_SCC_CELL_ENABLE)) {
			fcr |= KL0_SCC_CELL_ENABLE;
			need_reset_scc = 1;
		}
		if (chan_mask & MACIO_FLAG_SCCA_ON) {
			fcr |= KL0_SCCA_ENABLE;
			/* Don't enable line drivers for I2S modem */
			if ((param & 0xfff) == PMAC_SCC_I2S1)
				fcr &= ~KL0_SCC_A_INTF_ENABLE;
			else
				fcr |= KL0_SCC_A_INTF_ENABLE;
		}
		if (chan_mask & MACIO_FLAG_SCCB_ON) {
			fcr |= KL0_SCCB_ENABLE;
			/* Perform irda specific inits */
			if ((param & 0xfff) == PMAC_SCC_IRDA) {
				fcr &= ~KL0_SCC_B_INTF_ENABLE;
				fcr |= KL0_IRDA_ENABLE;
				fcr |= KL0_IRDA_CLK32_ENABLE | KL0_IRDA_CLK19_ENABLE;
				fcr |= KL0_IRDA_SOURCE1_SEL;
				fcr &= ~(KL0_IRDA_FAST_CONNECT|KL0_IRDA_DEFAULT1|KL0_IRDA_DEFAULT0);
				fcr &= ~(KL0_IRDA_SOURCE2_SEL|KL0_IRDA_HIGH_BAND);
				need_reset_irda = 1;
			} else
				fcr |= KL0_SCC_B_INTF_ENABLE;
		}
		MACIO_OUT32(KEYLARGO_FCR0, fcr);
		macio->flags |= chan_mask;
		if (need_reset_scc)  {
			MACIO_BIS(KEYLARGO_FCR0, KL0_SCC_RESET);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			UNLOCK(flags);
			mdelay(15);
			LOCK(flags);
			MACIO_BIC(KEYLARGO_FCR0, KL0_SCC_RESET);
		}
		if (need_reset_irda)  {
			MACIO_BIS(KEYLARGO_FCR0, KL0_IRDA_RESET);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			UNLOCK(flags);
			mdelay(15);
			LOCK(flags);
			MACIO_BIC(KEYLARGO_FCR0, KL0_IRDA_RESET);
		}
		UNLOCK(flags);
		if (param & PMAC_SCC_FLAG_XMON)
			macio->flags |= MACIO_FLAG_SCC_LOCKED;
	} else {
		if (macio->flags & MACIO_FLAG_SCC_LOCKED)
			return -EPERM;
		LOCK(flags);
		fcr = MACIO_IN32(KEYLARGO_FCR0);
		if (chan_mask & MACIO_FLAG_SCCA_ON)
			fcr &= ~KL0_SCCA_ENABLE;
		if (chan_mask & MACIO_FLAG_SCCB_ON) {
			fcr &= ~KL0_SCCB_ENABLE;
			/* Perform irda specific clears */
			if ((param & 0xfff) == PMAC_SCC_IRDA) {
				fcr &= ~KL0_IRDA_ENABLE;
				fcr &= ~(KL0_IRDA_CLK32_ENABLE | KL0_IRDA_CLK19_ENABLE);
				fcr &= ~(KL0_IRDA_FAST_CONNECT|KL0_IRDA_DEFAULT1|KL0_IRDA_DEFAULT0);
				fcr &= ~(KL0_IRDA_SOURCE1_SEL|KL0_IRDA_SOURCE2_SEL|KL0_IRDA_HIGH_BAND);
			}
		}
		MACIO_OUT32(KEYLARGO_FCR0, fcr);
		if ((fcr & (KL0_SCCA_ENABLE | KL0_SCCB_ENABLE)) == 0) {
			fcr &= ~KL0_SCC_CELL_ENABLE;
			MACIO_OUT32(KEYLARGO_FCR0, fcr);
		}
		macio->flags &= ~(chan_mask);
		UNLOCK(flags);
		mdelay(10);
	}
	return 0;
}

static int __pmac
core99_modem_enable(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio;
	u8			gpio;
	unsigned long		flags;
	
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	gpio = MACIO_IN8(KL_GPIO_MODEM_RESET);
	gpio |= KEYLARGO_GPIO_OUTPUT_ENABLE;
	gpio &= ~KEYLARGO_GPIO_OUTOUT_DATA;
	
	if (!value) {
		LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio);
		UNLOCK(flags);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		mdelay(250);
	}
    	LOCK(flags);
    	if (value) {
    		MACIO_BIC(KEYLARGO_FCR2, KL2_ALT_DATA_OUT);
	    	UNLOCK(flags);
	    	(void)MACIO_IN32(KEYLARGO_FCR2);
		mdelay(250);
    	} else {
    		MACIO_BIS(KEYLARGO_FCR2, KL2_ALT_DATA_OUT);
	    	UNLOCK(flags);
    	}
	if (value) {
		LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio | KEYLARGO_GPIO_OUTOUT_DATA);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
	    	UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
	    	UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio | KEYLARGO_GPIO_OUTOUT_DATA);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
	    	UNLOCK(flags); mdelay(250); LOCK(flags);
	}
	return 0;
}

static int __pmac
core99_ide_enable(struct device_node* node, int param, int value)
{
	switch(param) {
	    case 0:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_EIDE0_ENABLE, value);
	    case 1:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_EIDE1_ENABLE, value);
	    case 2:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_UIDE_ENABLE, value);
	    default:
	    	return -ENODEV;
	}
}

static int __pmac
core99_ide_reset(struct device_node* node, int param, int value)
{
	switch(param) {
	    case 0:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_EIDE0_RESET_N, !value);
	    case 1:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_EIDE1_RESET_N, !value);
	    case 2:
		return simple_feature_tweak(node, macio_unknown,
			KEYLARGO_FCR1, KL1_UIDE_RESET_N, !value);
	    default:
	    	return -ENODEV;
	}
}

static int __pmac
core99_gmac_enable(struct device_node* node, int param, int value)
{
	unsigned long flags;

	LOCK(flags);
	if (value)
		UN_BIS(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_GMAC);
	else
		UN_BIC(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_GMAC);
	(void)UN_IN(UNI_N_CLOCK_CNTL);
	UNLOCK(flags);
	udelay(20);

	return 0;
}

static int __pmac
core99_gmac_phy_reset(struct device_node* node, int param, int value)
{
	unsigned long flags;
	struct macio_chip* macio;
	
	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea)
		return -ENODEV;

	LOCK(flags);
	MACIO_OUT8(KL_GPIO_ETH_PHY_RESET, KEYLARGO_GPIO_OUTPUT_ENABLE);
	(void)MACIO_IN8(KL_GPIO_ETH_PHY_RESET);
	UNLOCK(flags);
	mdelay(10);
	LOCK(flags);
	MACIO_OUT8(KL_GPIO_ETH_PHY_RESET, KEYLARGO_GPIO_OUTPUT_ENABLE
		| KEYLARGO_GPIO_OUTOUT_DATA);
	UNLOCK(flags);
	mdelay(10);

	return 0;
}

static int __pmac
core99_sound_chip_enable(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio;
	unsigned long		flags;
	
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;

	/* Do a better probe code, screamer G4 desktops &
	 * iMacs can do that too, add a recalibrate  in
	 * the driver as well
	 */
	if (pmac_mb.model_id == PMAC_TYPE_PISMO ||
	    pmac_mb.model_id == PMAC_TYPE_TITANIUM) {
		LOCK(flags);
		if (value)
	    		MACIO_OUT8(KL_GPIO_SOUND_POWER,
	    			KEYLARGO_GPIO_OUTPUT_ENABLE |
	    			KEYLARGO_GPIO_OUTOUT_DATA);
	    	else
	    		MACIO_OUT8(KL_GPIO_SOUND_POWER,
	    			KEYLARGO_GPIO_OUTPUT_ENABLE);
	    	(void)MACIO_IN8(KL_GPIO_SOUND_POWER);
	    	UNLOCK(flags);
	}
	return 0;
}

static int __pmac
core99_airport_enable(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio;
	unsigned long		flags;
	int			state;
	
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	
	/* Hint: we allow passing of macio itself for the sake of the
	 * sleep code
	 */
	if (node != macio->of_node &&
	    (!node->parent || node->parent != macio->of_node))
		return -ENODEV;
	state = (macio->flags & MACIO_FLAG_AIRPORT_ON) != 0;
	if (value == state)
		return 0;
	if (value) {
		/* This code is a reproduction of OF enable-cardslot
		 * and init-wireless methods, slightly hacked until
		 * I got it working.
		 */
		LOCK(flags);
		MACIO_OUT8(KEYLARGO_GPIO_0+0xf, 5);
		(void)MACIO_IN8(KEYLARGO_GPIO_0+0xf);
		UNLOCK(flags);
		mdelay(10);
		LOCK(flags);
		MACIO_OUT8(KEYLARGO_GPIO_0+0xf, 4);
		(void)MACIO_IN8(KEYLARGO_GPIO_0+0xf);
		UNLOCK(flags);

		mdelay(10);

		LOCK(flags);
		MACIO_BIC(KEYLARGO_FCR2, KL2_CARDSEL_16);
		(void)MACIO_IN32(KEYLARGO_FCR2);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_EXTINT_0+0xb, 0);
		(void)MACIO_IN8(KEYLARGO_GPIO_EXTINT_0+0xb);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_EXTINT_0+0xa, 0x28);
		(void)MACIO_IN8(KEYLARGO_GPIO_EXTINT_0+0xa);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_EXTINT_0+0xd, 0x28);
		(void)MACIO_IN8(KEYLARGO_GPIO_EXTINT_0+0xd);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_0+0xd, 0x28);
		(void)MACIO_IN8(KEYLARGO_GPIO_0+0xd);
		udelay(10);
		MACIO_OUT8(KEYLARGO_GPIO_0+0xe, 0x28);
		(void)MACIO_IN8(KEYLARGO_GPIO_0+0xe);
		UNLOCK(flags);
		udelay(10);
		MACIO_OUT32(0x1c000, 0);
		mdelay(1);
		MACIO_OUT8(0x1a3e0, 0x41);
		(void)MACIO_IN8(0x1a3e0);
		udelay(10);
		LOCK(flags);
		MACIO_BIS(KEYLARGO_FCR2, KL2_CARDSEL_16);
		(void)MACIO_IN32(KEYLARGO_FCR2);
		UNLOCK(flags);
		mdelay(100);

		macio->flags |= MACIO_FLAG_AIRPORT_ON;
	} else {
		LOCK(flags);
		MACIO_BIC(KEYLARGO_FCR2, KL2_CARDSEL_16);
		(void)MACIO_IN32(KEYLARGO_FCR2);
		MACIO_OUT8(KL_GPIO_AIRPORT_0, 0);
		MACIO_OUT8(KL_GPIO_AIRPORT_1, 0);
		MACIO_OUT8(KL_GPIO_AIRPORT_2, 0);
		MACIO_OUT8(KL_GPIO_AIRPORT_3, 0);
		MACIO_OUT8(KL_GPIO_AIRPORT_4, 0);
		(void)MACIO_IN8(KL_GPIO_AIRPORT_4);
		UNLOCK(flags);

		macio->flags &= ~MACIO_FLAG_AIRPORT_ON;
	}
	return 0;
}

#ifdef CONFIG_SMP
static int __pmac
core99_reset_cpu(struct device_node* node, int param, int value)
{
	const int reset_lines[] = {	KL_GPIO_RESET_CPU0,
					KL_GPIO_RESET_CPU1,
					KL_GPIO_RESET_CPU2,
					KL_GPIO_RESET_CPU3 };
	int reset_io;
	unsigned long flags;
	struct macio_chip* macio;
	
	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea)
		return -ENODEV;
	if (param > 3 || param < 0)
		return -ENODEV;

	reset_io = reset_lines[param];
	
	LOCK(flags);
	MACIO_OUT8(reset_io, KEYLARGO_GPIO_OUTPUT_ENABLE);
	(void)MACIO_IN8(reset_io);
	udelay(1);
	MACIO_OUT8(reset_io, KEYLARGO_GPIO_OUTPUT_ENABLE | KEYLARGO_GPIO_OUTOUT_DATA);
	(void)MACIO_IN8(reset_io);
	UNLOCK(flags);

	return 0;
}
#endif /* CONFIG_SMP */

static int __pmac
core99_usb_enable(struct device_node* node, int param, int value)
{
	struct macio_chip* macio;
	unsigned long flags;
	char* prop;
	int number;
	u32 reg;
	
	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea)
		return -ENODEV;
	
	prop = (char *)get_property(node, "AAPL,clock-id", NULL);
	if (!prop)
		return -ENODEV;
	if (strncmp(prop, "usb0u048", strlen("usb0u048")) == 0)
		number = 0;
	else if (strncmp(prop, "usb1u148", strlen("usb1u148")) == 0)
		number = 2;
	else
		return -ENODEV;

	/* Sorry for the brute-force locking, but this is only used during
	 * sleep and the timing seem to be critical
	 */
	LOCK(flags);
	if (value) {
		/* Turn ON */
		if (number == 0) {
			MACIO_BIC(KEYLARGO_FCR0, (KL0_USB0_PAD_SUSPEND0 | KL0_USB0_PAD_SUSPEND1));
			(void)MACIO_IN32(KEYLARGO_FCR0);
			UNLOCK(flags);
			mdelay(1);
			LOCK(flags);
			MACIO_BIS(KEYLARGO_FCR0, KL0_USB0_CELL_ENABLE);
		} else {
			MACIO_BIC(KEYLARGO_FCR0, (KL0_USB1_PAD_SUSPEND0 | KL0_USB1_PAD_SUSPEND1));
			UNLOCK(flags);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			mdelay(1);
			LOCK(flags);
			MACIO_BIS(KEYLARGO_FCR0, KL0_USB1_CELL_ENABLE);
		}
		reg = MACIO_IN32(KEYLARGO_FCR4);
		reg &=	~(KL4_PORT_WAKEUP_ENABLE(number) | KL4_PORT_RESUME_WAKE_EN(number) |
			KL4_PORT_CONNECT_WAKE_EN(number) | KL4_PORT_DISCONNECT_WAKE_EN(number));
		reg &=	~(KL4_PORT_WAKEUP_ENABLE(number+1) | KL4_PORT_RESUME_WAKE_EN(number+1) |
			KL4_PORT_CONNECT_WAKE_EN(number+1) | KL4_PORT_DISCONNECT_WAKE_EN(number+1));
		MACIO_OUT32(KEYLARGO_FCR4, reg);
		(void)MACIO_IN32(KEYLARGO_FCR4);
		udelay(10);
	} else {
		/* Turn OFF */
		reg = MACIO_IN32(KEYLARGO_FCR4);
		reg |=	KL4_PORT_WAKEUP_ENABLE(number) | KL4_PORT_RESUME_WAKE_EN(number) |
			KL4_PORT_CONNECT_WAKE_EN(number) | KL4_PORT_DISCONNECT_WAKE_EN(number);
		reg |=	KL4_PORT_WAKEUP_ENABLE(number+1) | KL4_PORT_RESUME_WAKE_EN(number+1) |
			KL4_PORT_CONNECT_WAKE_EN(number+1) | KL4_PORT_DISCONNECT_WAKE_EN(number+1);
		MACIO_OUT32(KEYLARGO_FCR4, reg);
		(void)MACIO_IN32(KEYLARGO_FCR4);
		udelay(1);
		if (number == 0) {
			MACIO_BIC(KEYLARGO_FCR0, KL0_USB0_CELL_ENABLE);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			udelay(1);
			MACIO_BIS(KEYLARGO_FCR0, (KL0_USB0_PAD_SUSPEND0 | KL0_USB0_PAD_SUSPEND1));
			(void)MACIO_IN32(KEYLARGO_FCR0);
		} else {
			MACIO_BIC(KEYLARGO_FCR0, KL0_USB1_CELL_ENABLE);
			(void)MACIO_IN32(KEYLARGO_FCR0);
			udelay(1);
			MACIO_BIS(KEYLARGO_FCR0, (KL0_USB1_PAD_SUSPEND0 | KL0_USB1_PAD_SUSPEND1));
			(void)MACIO_IN32(KEYLARGO_FCR0);
		}
		udelay(1);
	}
	UNLOCK(flags);

	return 0;
}

static int __pmac
core99_firewire_enable(struct device_node* node, int param, int value)
{
	unsigned long flags;
	struct macio_chip* macio;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea)
		return -ENODEV;
	if (!(macio->flags & MACIO_FLAG_FW_SUPPORTED))
		return -ENODEV;
	
	LOCK(flags);
	if (value) {
		UN_BIS(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_FW);
		(void)UN_IN(UNI_N_CLOCK_CNTL);
	} else {
		UN_BIC(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_FW);
		(void)UN_IN(UNI_N_CLOCK_CNTL);
	}
	UNLOCK(flags);
	mdelay(1);

	return 0;
}

static int __pmac
core99_firewire_cable_power(struct device_node* node, int param, int value)
{
	unsigned long flags;
	struct macio_chip* macio;

	/* Trick: we allow NULL node */
	if ((pmac_mb.board_flags & PMAC_MB_HAS_FW_POWER) == 0)
	    	return -ENODEV;
	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea)
		return -ENODEV;
	if (!(macio->flags & MACIO_FLAG_FW_SUPPORTED))
		return -ENODEV;
	
	LOCK(flags);
	if (value) {
		MACIO_OUT8(KL_GPIO_FW_CABLE_POWER , 0);
		MACIO_IN8(KL_GPIO_FW_CABLE_POWER);
		udelay(10);
	} else {
		MACIO_OUT8(KL_GPIO_FW_CABLE_POWER , 4);
		MACIO_IN8(KL_GPIO_FW_CABLE_POWER); udelay(10);
	}
	UNLOCK(flags);
	mdelay(1);

	return 0;
}

static int __pmac
core99_read_gpio(struct device_node* node, int param, int value)
{
	struct macio_chip* macio = &macio_chips[0];
	
	return MACIO_IN8(param);
}


static int __pmac
core99_write_gpio(struct device_node* node, int param, int value)
{
	struct macio_chip* macio = &macio_chips[0];

	MACIO_OUT8(param, (u8)(value & 0xff));
	return 0;
}

static void __pmac
keylargo_shutdown(struct macio_chip* macio, int restart)
{
	u32 temp;

	mdelay(1);
	MACIO_BIS(KEYLARGO_FCR0, KL0_USB_REF_SUSPEND);
	(void)MACIO_IN32(KEYLARGO_FCR0);
	mdelay(100);

	MACIO_BIC(KEYLARGO_FCR0,KL0_SCCA_ENABLE | KL0_SCCB_ENABLE |
				KL0_SCC_CELL_ENABLE |
		      		KL0_IRDA_ENABLE | KL0_IRDA_CLK32_ENABLE |
		      		KL0_IRDA_CLK19_ENABLE);

	(void)MACIO_IN32(KEYLARGO_FCR0); udelay(10);
	MACIO_BIC(KEYLARGO_MBCR, KL_MBCR_MB0_DEV_MASK);
	(void)MACIO_IN32(KEYLARGO_MBCR); udelay(10);

	MACIO_BIC(KEYLARGO_FCR1,
		KL1_AUDIO_SEL_22MCLK | KL1_AUDIO_CLK_ENABLE_BIT |
		KL1_AUDIO_CLK_OUT_ENABLE | KL1_AUDIO_CELL_ENABLE |
		KL1_I2S0_CELL_ENABLE | KL1_I2S0_CLK_ENABLE_BIT |
		KL1_I2S0_ENABLE | KL1_I2S1_CELL_ENABLE |
		KL1_I2S1_CLK_ENABLE_BIT | KL1_I2S1_ENABLE |
		KL1_EIDE0_ENABLE | KL1_EIDE0_RESET_N |
		KL1_EIDE1_ENABLE | KL1_EIDE1_RESET_N |
		KL1_UIDE_ENABLE);
	(void)MACIO_IN32(KEYLARGO_FCR1); udelay(10);

	MACIO_BIS(KEYLARGO_FCR2, KL2_ALT_DATA_OUT);
 	udelay(10);
 	MACIO_BIC(KEYLARGO_FCR2, KL2_IOBUS_ENABLE);
 	udelay(10);
	temp = MACIO_IN32(KEYLARGO_FCR3);
	if (macio->rev >= 2)
		temp |= (KL3_SHUTDOWN_PLL2X | KL3_SHUTDOWN_PLL_TOTAL);
		
	temp |= KL3_SHUTDOWN_PLLKW6 | KL3_SHUTDOWN_PLLKW4 |
		KL3_SHUTDOWN_PLLKW35 | KL3_SHUTDOWN_PLLKW12;
	temp &= ~(KL3_CLK66_ENABLE | KL3_CLK49_ENABLE | KL3_CLK45_ENABLE
		| KL3_CLK31_ENABLE | KL3_TIMER_CLK18_ENABLE | KL3_I2S1_CLK18_ENABLE
		| KL3_I2S0_CLK18_ENABLE | KL3_VIA_CLK16_ENABLE);
	MACIO_OUT32(KEYLARGO_FCR3, temp);
	(void)MACIO_IN32(KEYLARGO_FCR3); udelay(10);
}

static void __pmac
pangea_shutdown(struct macio_chip* macio, int restart)
{
	u32 temp;

	MACIO_BIC(KEYLARGO_FCR0,KL0_SCCA_ENABLE | KL0_SCCB_ENABLE |
				KL0_SCC_CELL_ENABLE |
				KL0_USB0_CELL_ENABLE | KL0_USB1_CELL_ENABLE);

	(void)MACIO_IN32(KEYLARGO_FCR0); udelay(10);
	MACIO_BIC(KEYLARGO_MBCR, KL_MBCR_MB0_DEV_MASK);
	(void)MACIO_IN32(KEYLARGO_MBCR); udelay(10);

	MACIO_BIC(KEYLARGO_FCR1,
		KL1_AUDIO_SEL_22MCLK | KL1_AUDIO_CLK_ENABLE_BIT |
		KL1_AUDIO_CLK_OUT_ENABLE | KL1_AUDIO_CELL_ENABLE |
		KL1_I2S0_CELL_ENABLE | KL1_I2S0_CLK_ENABLE_BIT |
		KL1_I2S0_ENABLE | KL1_I2S1_CELL_ENABLE |
		KL1_I2S1_CLK_ENABLE_BIT | KL1_I2S1_ENABLE |
		KL1_UIDE_ENABLE);
	(void)MACIO_IN32(KEYLARGO_FCR1); udelay(10);

	MACIO_BIS(KEYLARGO_FCR2, KL2_ALT_DATA_OUT);
 	udelay(10);
	temp = MACIO_IN32(KEYLARGO_FCR3);
	temp |= KL3_SHUTDOWN_PLLKW6 | KL3_SHUTDOWN_PLLKW4 |
		KL3_SHUTDOWN_PLLKW35;
	temp &= ~(KL3_CLK49_ENABLE | KL3_CLK45_ENABLE
		| KL3_CLK31_ENABLE | KL3_TIMER_CLK18_ENABLE | KL3_I2S1_CLK18_ENABLE
		| KL3_I2S0_CLK18_ENABLE | KL3_VIA_CLK16_ENABLE);
	MACIO_OUT32(KEYLARGO_FCR3, temp);
	(void)MACIO_IN32(KEYLARGO_FCR3); udelay(10);
}

static int __pmac
core99_sleep(void)
{
	struct macio_chip* macio;
	int i;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea)
		return -ENODEV;
	
	/* We power off the wireless slot in case it was not done
	 * by the driver. We don't power it on automatically however
	 */
	if (macio->flags & MACIO_FLAG_AIRPORT_ON)
		core99_airport_enable(macio->of_node, 0, 0);

	/* We power off the FW cable. Should be done by the driver... */
	if (macio->flags & MACIO_FLAG_FW_SUPPORTED) {
		core99_firewire_enable(NULL, 0, 0);
		core99_firewire_cable_power(NULL, 0, 0);
	}

	/* We make sure int. modem is off (in case driver lost it) */
	core99_modem_enable(macio->of_node, 0, 0);
	/* We make sure the sound is off as well */
	core99_sound_chip_enable(macio->of_node, 0, 0);
	 
	/*
	 * Save various bits of KeyLargo
	 */

	/* Save the state of the various GPIOs */
	save_gpio_levels[0] = MACIO_IN32(KEYLARGO_GPIO_LEVELS0);
	save_gpio_levels[1] = MACIO_IN32(KEYLARGO_GPIO_LEVELS1);
	for (i=0; i<KEYLARGO_GPIO_EXTINT_CNT; i++)
		save_gpio_extint[i] = MACIO_IN8(KEYLARGO_GPIO_EXTINT_0+i);
	for (i=0; i<KEYLARGO_GPIO_CNT; i++)
		save_gpio_normal[i] = MACIO_IN8(KEYLARGO_GPIO_0+i);

	/* Save the FCRs */
	save_mbcr = MACIO_IN32(KEYLARGO_MBCR);
	save_fcr[0] = MACIO_IN32(KEYLARGO_FCR0);
	save_fcr[1] = MACIO_IN32(KEYLARGO_FCR1);
	save_fcr[2] = MACIO_IN32(KEYLARGO_FCR2);
	save_fcr[3] = MACIO_IN32(KEYLARGO_FCR3);
	save_fcr[4] = MACIO_IN32(KEYLARGO_FCR4);

	/* Save state & config of DBDMA channels */
	dbdma_save(macio, save_dbdma);
	
	/*
	 * Turn off as much as we can
	 */
	if (macio->type == macio_pangea)
		pangea_shutdown(macio, 0);
	else if (macio->type == macio_keylargo)
		keylargo_shutdown(macio, 0);
	
	/* 
	 * Put the host bridge to sleep
	 */

	save_unin_clock_ctl = UN_IN(UNI_N_CLOCK_CNTL);
	UN_OUT(UNI_N_CLOCK_CNTL, save_unin_clock_ctl &
		~(UNI_N_CLOCK_CNTL_GMAC|UNI_N_CLOCK_CNTL_FW/*|UNI_N_CLOCK_CNTL_PCI*/));
	udelay(100);
	UN_OUT(UNI_N_HWINIT_STATE, UNI_N_HWINIT_STATE_SLEEPING);
	UN_OUT(UNI_N_POWER_MGT, UNI_N_POWER_MGT_SLEEP);

	/*
	 * FIXME: A bit of black magic with OpenPIC (don't ask me why)
	 */
	if (pmac_mb.model_id == PMAC_TYPE_SAWTOOTH) {
		MACIO_BIS(0x506e0, 0x00400000);
		MACIO_BIS(0x506e0, 0x80000000);
	}
	return 0;
}

static int __pmac
core99_wake_up(void)
{
	struct macio_chip* macio;
	int i;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo && macio->type != macio_pangea)
		return -ENODEV;

	/*
	 * Wakeup the host bridge
	 */
	UN_OUT(UNI_N_POWER_MGT, UNI_N_POWER_MGT_NORMAL);
	udelay(10);
	UN_OUT(UNI_N_HWINIT_STATE, UNI_N_HWINIT_STATE_RUNNING);
	udelay(10);
	
	/*
	 * Restore KeyLargo
	 */
	 
	MACIO_OUT32(KEYLARGO_MBCR, save_mbcr);
	(void)MACIO_IN32(KEYLARGO_MBCR); udelay(10);
	MACIO_OUT32(KEYLARGO_FCR0, save_fcr[0]);
	(void)MACIO_IN32(KEYLARGO_FCR0); udelay(10);
	MACIO_OUT32(KEYLARGO_FCR1, save_fcr[1]);
	(void)MACIO_IN32(KEYLARGO_FCR1); udelay(10);
	MACIO_OUT32(KEYLARGO_FCR2, save_fcr[2]);
	(void)MACIO_IN32(KEYLARGO_FCR2); udelay(10);
	MACIO_OUT32(KEYLARGO_FCR3, save_fcr[3]);
	(void)MACIO_IN32(KEYLARGO_FCR3); udelay(10);
	MACIO_OUT32(KEYLARGO_FCR4, save_fcr[4]);
	(void)MACIO_IN32(KEYLARGO_FCR4); udelay(10);

	dbdma_restore(macio, save_dbdma);

	MACIO_OUT32(KEYLARGO_GPIO_LEVELS0, save_gpio_levels[0]);
	MACIO_OUT32(KEYLARGO_GPIO_LEVELS1, save_gpio_levels[1]);
	for (i=0; i<KEYLARGO_GPIO_EXTINT_CNT; i++)
		MACIO_OUT8(KEYLARGO_GPIO_EXTINT_0+i, save_gpio_extint[i]);
	for (i=0; i<KEYLARGO_GPIO_CNT; i++)
		MACIO_OUT8(KEYLARGO_GPIO_0+i, save_gpio_normal[i]);

	/* FIXME more black magic with OpenPIC ... */
	if (pmac_mb.model_id == PMAC_TYPE_SAWTOOTH) {
		MACIO_BIC(0x506e0, 0x00400000);
		MACIO_BIC(0x506e0, 0x80000000);
	}

	UN_OUT(UNI_N_CLOCK_CNTL, save_unin_clock_ctl);
	udelay(100);

	return 0;
}

static int __pmac
core99_sleep_state(struct device_node* node, int param, int value)
{
	if ((pmac_mb.board_flags & PMAC_MB_CAN_SLEEP) == 0)
		return -EPERM;
	if (value == 1)
		return core99_sleep();
	else if (value == 0)
		return core99_wake_up();
	return 0;
}

static int __pmac
pangea_modem_enable(struct device_node* node, int param, int value)
{
	struct macio_chip*	macio;
	u8			gpio;
	unsigned long		flags;
	
	macio = macio_find(node, 0);
	if (!macio)
		return -ENODEV;
	gpio = MACIO_IN8(KL_GPIO_MODEM_RESET);
	gpio |= KEYLARGO_GPIO_OUTPUT_ENABLE;
	gpio &= ~KEYLARGO_GPIO_OUTOUT_DATA;
	
	if (!value) {
		LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio);
		UNLOCK(flags);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
		mdelay(250);
	}
    	LOCK(flags);
	if (value) {
		MACIO_OUT8(KL_GPIO_MODEM_POWER,
			KEYLARGO_GPIO_OUTPUT_ENABLE);
    		UNLOCK(flags);
	    	(void)MACIO_IN32(KEYLARGO_FCR2);
		mdelay(250);
	} else {
		MACIO_OUT8(KL_GPIO_MODEM_POWER,
			KEYLARGO_GPIO_OUTPUT_ENABLE | KEYLARGO_GPIO_OUTOUT_DATA);
    		UNLOCK(flags);
	}
	if (value) {
		LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio | KEYLARGO_GPIO_OUTOUT_DATA);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
	    	UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
	    	UNLOCK(flags); mdelay(250); LOCK(flags);
		MACIO_OUT8(KL_GPIO_MODEM_RESET, gpio | KEYLARGO_GPIO_OUTOUT_DATA);
		(void)MACIO_IN8(KL_GPIO_MODEM_RESET);
	    	UNLOCK(flags); mdelay(250); LOCK(flags);
	}
	return 0;
}


static int __pmac
generic_get_mb_info(struct device_node* node, int param, int value)
{
	switch(param) {
		case PMAC_MB_INFO_MODEL:
			return pmac_mb.model_id;
		case PMAC_MB_INFO_FLAGS:
			return pmac_mb.board_flags;
		case PMAC_MB_INFO_NAME:	
			/* hack hack hack... but should work */
			return (int)pmac_mb.model_name;
	}
	return 0;
}


/* 
 * Table definitions
 */
 
/* Used on any machine
 */
static struct feature_table_entry any_features[]  __pmacdata = {
	{ PMAC_FTR_GET_MB_INFO,		generic_get_mb_info },
	{ 0, NULL }
};

/* OHare based motherboards. Currently, we only use these on the
 * 2400,3400 and 3500 series powerbooks. Some older desktops seem
 * to have issues with turning on/off those asic cells
 */
static struct feature_table_entry ohare_features[]  __pmacdata = {
	{ PMAC_FTR_SCC_ENABLE,		ohare_scc_enable },
	{ PMAC_FTR_SWIM3_ENABLE,	ohare_floppy_enable },
	{ PMAC_FTR_MESH_ENABLE,		ohare_mesh_enable },
	{ PMAC_FTR_IDE_ENABLE,		ohare_ide_enable},
	{ PMAC_FTR_IDE_RESET,		ohare_ide_reset},
	{ PMAC_FTR_SLEEP_STATE,		ohare_sleep_state },
	{ 0, NULL }
};

/* Heathrow desktop machines (Beige G3).
 * Separated as some features couldn't be properly tested
 * and the serial port control bits appear to confuse it.
 */
static struct feature_table_entry heathrow_desktop_features[]  __pmacdata = {
	{ PMAC_FTR_SWIM3_ENABLE,	heathrow_floppy_enable },
	{ PMAC_FTR_MESH_ENABLE,		heathrow_mesh_enable },
	{ PMAC_FTR_IDE_ENABLE,		heathrow_ide_enable },
	{ PMAC_FTR_IDE_RESET,		heathrow_ide_reset },
	{ PMAC_FTR_BMAC_ENABLE,		heathrow_bmac_enable },
	{ 0, NULL }
};

/* Heathrow based laptop, that is the Wallstreet and mainstreet
 * powerbooks.
 */
static struct feature_table_entry heathrow_laptop_features[]  __pmacdata = {
	{ PMAC_FTR_SCC_ENABLE,		heathrow_scc_enable },
	{ PMAC_FTR_MODEM_ENABLE,	heathrow_modem_enable },
	{ PMAC_FTR_SWIM3_ENABLE,	heathrow_floppy_enable },
	{ PMAC_FTR_MESH_ENABLE,		heathrow_mesh_enable },
	{ PMAC_FTR_IDE_ENABLE,		heathrow_ide_enable },
	{ PMAC_FTR_IDE_RESET,		heathrow_ide_reset },
	{ PMAC_FTR_BMAC_ENABLE,		heathrow_bmac_enable },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	heathrow_sound_enable },
	{ PMAC_FTR_SLEEP_STATE,		heathrow_sleep_state },
	{ 0, NULL }
};

/* Paddington based machines
 * The lombard (101) powerbook, first iMac models, B&W G3 and Yikes G4.
 */
static struct feature_table_entry paddington_features[]  __pmacdata = {
	{ PMAC_FTR_SCC_ENABLE,		heathrow_scc_enable },
	{ PMAC_FTR_MODEM_ENABLE,	heathrow_modem_enable },
	{ PMAC_FTR_SWIM3_ENABLE,	heathrow_floppy_enable },
	{ PMAC_FTR_MESH_ENABLE,		heathrow_mesh_enable },
	{ PMAC_FTR_IDE_ENABLE,		heathrow_ide_enable },
	{ PMAC_FTR_IDE_RESET,		heathrow_ide_reset },
	{ PMAC_FTR_BMAC_ENABLE,		heathrow_bmac_enable },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	heathrow_sound_enable },
	{ PMAC_FTR_SLEEP_STATE,		heathrow_sleep_state },
	{ 0, NULL }
};

/* Core99 & MacRISC 2 machines (all machines released since the
 * iBook (included), that is all AGP machines, except pangea
 * chipset. The pangea chipset is the "combo" UniNorth/KeyLargo
 * used on iBook2 & iMac "flow power".
 */
static struct feature_table_entry core99_features[]  __pmacdata = {
	{ PMAC_FTR_SCC_ENABLE,		core99_scc_enable },
	{ PMAC_FTR_MODEM_ENABLE,	core99_modem_enable },
	{ PMAC_FTR_IDE_ENABLE,		core99_ide_enable },
	{ PMAC_FTR_IDE_RESET,		core99_ide_reset },
	{ PMAC_FTR_GMAC_ENABLE,		core99_gmac_enable },
	{ PMAC_FTR_GMAC_PHY_RESET,	core99_gmac_phy_reset },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	core99_sound_chip_enable },
	{ PMAC_FTR_AIRPORT_ENABLE,	core99_airport_enable },
	{ PMAC_FTR_USB_ENABLE,		core99_usb_enable },
	{ PMAC_FTR_1394_ENABLE,		core99_firewire_enable },
	{ PMAC_FTR_1394_CABLE_POWER,	core99_firewire_cable_power },
	{ PMAC_FTR_SLEEP_STATE,		core99_sleep_state },
#ifdef CONFIG_SMP
	{ PMAC_FTR_RESET_CPU,		core99_reset_cpu },
#endif /* CONFIG_SMP */
	{ PMAC_FTR_READ_GPIO,		core99_read_gpio },
	{ PMAC_FTR_WRITE_GPIO,		core99_write_gpio },
	{ 0, NULL }
};

/* Pangea features
 */
static struct feature_table_entry pangea_features[]  __pmacdata = {
	{ PMAC_FTR_SCC_ENABLE,		core99_scc_enable },
	{ PMAC_FTR_MODEM_ENABLE,	pangea_modem_enable },
	{ PMAC_FTR_IDE_ENABLE,		core99_ide_enable },
	{ PMAC_FTR_IDE_RESET,		core99_ide_reset },
	{ PMAC_FTR_GMAC_ENABLE,		core99_gmac_enable },
	{ PMAC_FTR_GMAC_PHY_RESET,	core99_gmac_phy_reset },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	core99_sound_chip_enable },
	{ PMAC_FTR_AIRPORT_ENABLE,	core99_airport_enable },
	{ PMAC_FTR_USB_ENABLE,		core99_usb_enable },
	{ PMAC_FTR_1394_ENABLE,		core99_firewire_enable },
	{ PMAC_FTR_1394_CABLE_POWER,	core99_firewire_cable_power },
	{ PMAC_FTR_SLEEP_STATE,		core99_sleep_state },
	{ PMAC_FTR_READ_GPIO,		core99_read_gpio },
	{ PMAC_FTR_WRITE_GPIO,		core99_write_gpio },
	{ 0, NULL }
};
	
static struct pmac_mb_def pmac_mb_defs[] __pmacdata = {
	/* Warning: ordering is important as some models may claim
	 * beeing compatible with several types
	 */
	{	"AAPL,8500",			"PowerMac 8500/8600",
		PMAC_TYPE_PSURGE,		NULL,
		0
	},
	{	"AAPL,9500",			"PowerMac 9500/9600",
		PMAC_TYPE_PSURGE,		NULL,
		0
	},
	{	"AAPL,7500",			"PowerMac 7500",
		PMAC_TYPE_PSURGE,		NULL,
		0
	},
	{	"AAPL,e407",			"Alchemy",
		PMAC_TYPE_ALCHEMY,		NULL,
		0
	},
	{	"AAPL,e411",			"Gazelle",
		PMAC_TYPE_GAZELLE,		NULL,
		0
	},
	{	"AAPL,3400/2400",		"PowerBook 3400",
		PMAC_TYPE_HOOPER,		ohare_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"AAPL,3500",			"PowerBook 3500",
		PMAC_TYPE_KANGA,		ohare_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"AAPL,Gossamer",		"PowerMac G3 (Gossamer)",
		PMAC_TYPE_GOSSAMER,		heathrow_desktop_features,
		0
	},
	{	"AAPL,PowerMac G3",		"PowerMac G3 (Silk)",
		PMAC_TYPE_SILK,			heathrow_desktop_features,
		0
	},
	{	"AAPL,PowerBook1998",		"PowerBook Wallstreet",
		PMAC_TYPE_WALLSTREET,		heathrow_laptop_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"AAPL,PowerBook1,1",		"PowerBook 101 (Lombard)",
		PMAC_TYPE_101_PBOOK,		paddington_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"iMac,1",			"iMac (first generation)",
		PMAC_TYPE_ORIG_IMAC,		paddington_features,
		0
	},
	{	"PowerMac4,1",			"iMac \"Flower Power\"",
		PMAC_TYPE_PANGEA_IMAC,		pangea_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"PowerBook4,1",			"iBook 2",
		PMAC_TYPE_IBOOK2,		pangea_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_HAS_FW_POWER
	},
	{	"PowerMac4,2",			"Flat panel iMac",
		PMAC_TYPE_FLAT_PANEL_IMAC,	pangea_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"PowerMac1,1",			"Blue&White G3",
		PMAC_TYPE_YOSEMITE,		paddington_features,
		0
	},
	{	"PowerMac1,2",			"PowerMac G4 PCI Graphics",
		PMAC_TYPE_YIKES,		paddington_features,
		0
	},
	{	"PowerBook2,1",			"iBook (first generation)",
		PMAC_TYPE_ORIG_IBOOK,		core99_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"PowerMac3,1",			"PowerMac G4 AGP Graphics",
		PMAC_TYPE_SAWTOOTH,		core99_features,
		0
	},
	{	"PowerMac3,2",			"PowerMac G4 AGP Graphics",
		PMAC_TYPE_SAWTOOTH,		core99_features,
		0
	},
	{	"PowerMac3,3",			"PowerMac G4 AGP Graphics",
		PMAC_TYPE_SAWTOOTH,		core99_features,
		0
	},
	{	"PowerMac2,1",			"iMac FireWire",
		PMAC_TYPE_FW_IMAC,		core99_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"PowerMac2,2",			"iMac FireWire",
		PMAC_TYPE_FW_IMAC,		core99_features,
		PMAC_MB_CAN_SLEEP
	},
	{	"PowerBook2,2",			"iBook FireWire",
		PMAC_TYPE_FW_IBOOK,		core99_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_HAS_FW_POWER
	},
	{	"PowerMac5,1",			"PowerMac G4 Cube",
		PMAC_TYPE_CUBE,			core99_features,
	},
	{	"PowerMac3,4",			"PowerMac G4 Silver",
		PMAC_TYPE_QUICKSILVER,		core99_features,
		0
	},
	{	"PowerMac3,5",			"PowerMac G4 Silver",
		PMAC_TYPE_QUICKSILVER,		core99_features,
		0
	},
	{	"PowerBook3,1",			"PowerBook Pismo",
		PMAC_TYPE_PISMO,		core99_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_HAS_FW_POWER
	},
	{	"PowerBook3,2",			"PowerBook Titanium",
		PMAC_TYPE_TITANIUM,		core99_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_HAS_FW_POWER
	},
	{	"PowerBook3,3",			"PowerBook Titanium II",
		PMAC_TYPE_TITANIUM2,		core99_features,
		PMAC_MB_CAN_SLEEP | PMAC_MB_HAS_FW_POWER
	},
};

/*
 * The toplevel feature_call callback
 */
int __pmac
pmac_do_feature_call(unsigned int selector, ...)
{
	struct device_node* node;
	int param, value, i;
	feature_call func = NULL;
	va_list args;
	
	if (!pmac_mb.features)
		return -ENODEV;
	for (i=0; pmac_mb.features[i].function; i++)
		if (pmac_mb.features[i].selector == selector) {
			func = pmac_mb.features[i].function;
			break;
		}
	if (!func)
		for (i=0; any_features[i].function; i++)
			if (any_features[i].selector == selector) {
				func = any_features[i].function;
				break;
			}
	if (!func)
		return -ENODEV;

	va_start(args, selector);
	node = (struct device_node*)va_arg(args, void*);
	param = va_arg(args, int);
	value = va_arg(args, int);
	va_end(args);

	return func(node, param, value);
}

static int __init
probe_motherboard(void)
{
	int i;
	struct macio_chip* macio = &macio_chips[0];

	/* Lookup known motherboard type in device-tree */
	for(i=0; i<(sizeof(pmac_mb_defs)/sizeof(struct pmac_mb_def)); i++) {
	    if (machine_is_compatible(pmac_mb_defs[i].model_string)) {
		pmac_mb = pmac_mb_defs[i];
		goto found;
	    }
	}

	/* Fallback to selection depending on mac-io chip type */
	switch(macio->type) {
	    case macio_grand_central:
		pmac_mb.model_id = PMAC_TYPE_PSURGE;
		pmac_mb.model_name = "Unknown PowerSurge";
		break;
	    case macio_ohare:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_OHARE;
		pmac_mb.model_name = "Unknown OHare-based";
	    	break;
	    case macio_heathrow:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_HEATHROW;
		pmac_mb.model_name = "Unknown Heathrow-based";
		pmac_mb.features = heathrow_desktop_features;
		break;
	    case macio_paddington:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_PADDINGTON;
		pmac_mb.model_name = "Unknown Paddington-based";
	    	pmac_mb.features = paddington_features;
		break;
	    case macio_keylargo:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_CORE99;
		pmac_mb.model_name = "Unknown Keylargo-based";
	    	pmac_mb.features = core99_features;
		break;
	    case macio_pangea:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_PANGEA;
		pmac_mb.model_name = "Unknown Pangea-based";
	    	pmac_mb.features = pangea_features;
		break;
	    default:
	    	return -ENODEV;
	}
found:
	/* Fixup Hooper vs. Comet */
	if (pmac_mb.model_id == PMAC_TYPE_HOOPER) {
		u32* mach_id_ptr = (u32*)ioremap(0xf3000034, 4);
		if (!mach_id_ptr)
			return -ENODEV;
		/* Here, I used to disable the media-bay on comet. It
		 * appears this is wrong, the floppy connector is actually
		 * a kind of media-bay and works with the current driver.
		 */
		if ((*mach_id_ptr) & 0x20000000UL)
			pmac_mb.model_id = PMAC_TYPE_COMET;
		iounmap(mach_id_ptr);
	}

	/* Set default value of powersave_nap on machines that support it.
	 * It appears that uninorth rev 3 has a problem with it, we don't
	 * enable it on those. In theory, the flush-on-lock property is
	 * supposed to be set when not supported, but I'm not very confident
	 * that all Apple OF revs did it properly, I do it the paranoid way.
	 */
	while (uninorth_base && uninorth_rev > 3) {
		struct device_node* np = find_path_device("/cpus");
		u32 pvr = mfspr(PVR);
		if (!np || !np->child) {
			printk(KERN_WARNING "Can't find CPU(s) in device tree !\n");
			break;
		}
		np = np->child;
		/* Nap mode not supported on SMP */
		if (np->sibling)
			break;
		/* Nap mode not supported if flush-on-lock property is present */
		if (get_property(np, "flush-on-lock", NULL))
			break;
		/* Some 7450 may have problem with NAP mode too ... */
		if (((pvr >> 16) == 0x8000) && ((pvr & 0xffff) < 0x0201))
			break;
		powersave_nap = 1;
		printk(KERN_INFO "Processor NAP mode on idle enabled.\n");
		break;
	}

	printk(KERN_INFO "PowerMac motherboard: %s\n", pmac_mb.model_name);
	return 0;
}

/* Initialize the Core99 UniNorth host bridge and memory controller
 */
static void __init
probe_uninorth(void)
{
	unsigned long actrl;
	
	/* Locate core99 Uni-N */
	uninorth_node = find_devices("uni-n");
	if (uninorth_node && uninorth_node->n_addrs > 0) {
		uninorth_base = ioremap(uninorth_node->addrs[0].address, 0x1000);
		uninorth_rev = in_be32(UN_REG(UNI_N_VERSION));
	} else
		uninorth_node = NULL;
		
	if (!uninorth_node)
		return;
	
	printk(KERN_INFO "Found Uninorth memory controller & host bridge, revision: %d\n",
			uninorth_rev);

	/* Set the arbitrer QAck delay according to what Apple does
	 */
	if (uninorth_rev < 0x10) {
		actrl = UN_IN(UNI_N_ARB_CTRL) & ~UNI_N_ARB_CTRL_QACK_DELAY_MASK;
		actrl |= ((uninorth_rev < 3) ? UNI_N_ARB_CTRL_QACK_DELAY105 :
			UNI_N_ARB_CTRL_QACK_DELAY) << UNI_N_ARB_CTRL_QACK_DELAY_SHIFT;
		UN_OUT(UNI_N_ARB_CTRL, actrl);
	}
}	

static void __init
probe_one_macio(const char* name, const char* compat, int type)
{
	struct device_node*	node;
	int			i;
	volatile u32*		base;
	u32*			revp;
	
	node = find_devices(name);
	if (!node || !node->n_addrs)
		return;
	if (compat)
		do {
			if (device_is_compatible(node, compat))
				break;
			node = node->next;
		} while (node);
	if (!node)
		return;
	for(i=0; i<MAX_MACIO_CHIPS; i++) {
		if (!macio_chips[i].of_node)
			break;
		if (macio_chips[i].of_node == node)
			return;
	}
	if (i >= MAX_MACIO_CHIPS) {
		printk(KERN_ERR "pmac_feature: Please increase MAX_MACIO_CHIPS !\n");
		printk(KERN_ERR "pmac_feature: %s skipped\n", node->full_name);
		return;
	}
	base = (volatile u32*)ioremap(node->addrs[0].address, node->addrs[0].size);
	if (!base) {
		printk(KERN_ERR "pmac_feature: Can't map mac-io chip !\n");
		return;
	}
	if (type == macio_keylargo) {
		u32* did = (u32 *)get_property(node, "device-id", NULL);
		if (*did == 0x00000025)
			type = macio_pangea;
	}
	macio_chips[i].of_node	= node;
	macio_chips[i].type	= type;
	macio_chips[i].base	= base;
	macio_chips[i].flags	= MACIO_FLAG_SCCB_ON | MACIO_FLAG_SCCB_ON;
	revp = (u32 *)get_property(node, "revision-id", NULL);
	if (revp)
		macio_chips[i].rev = *revp;
	printk(KERN_INFO "Found a %s mac-io controller, rev: %d, mapped at 0x%p\n",
		macio_names[type], macio_chips[i].rev, macio_chips[i].base);
}

static int __init
probe_macios(void)
{
	/* Warning, ordering is important */
	probe_one_macio("gc", NULL, macio_grand_central);
	probe_one_macio("ohare", NULL, macio_ohare);
	probe_one_macio("pci106b,7", NULL, macio_ohareII);
	probe_one_macio("mac-io", "keylargo", macio_keylargo);
	probe_one_macio("mac-io", "paddington", macio_paddington);
	probe_one_macio("mac-io", "gatwick", macio_gatwick);
	probe_one_macio("mac-io", "heathrow", macio_heathrow);

	/* Make sure the "main" macio chip appear first */
	if (macio_chips[0].type == macio_gatwick
	    && macio_chips[1].type == macio_heathrow) {
		struct macio_chip temp = macio_chips[0];
		macio_chips[0] = macio_chips[1];
		macio_chips[1] = temp;
	}
	if (macio_chips[0].type == macio_ohareII
	    && macio_chips[1].type == macio_ohare) {
		struct macio_chip temp = macio_chips[0];
		macio_chips[0] = macio_chips[1];
		macio_chips[1] = temp;
	}

	return (macio_chips[0].of_node == NULL) ? -ENODEV : 0;
}

static void __init
initial_serial_shutdown(struct device_node* np)
{
	int len;
	struct slot_names_prop {
		int	count;
		char	name[1];
	} *slots;
	char *conn;
	int port_type = PMAC_SCC_ASYNC;
	int modem = 0;
	
	slots = (struct slot_names_prop *)get_property(np, "slot-names", &len);
	conn = get_property(np, "AAPL,connector", &len);
	if (conn && (strcmp(conn, "infrared") == 0))
		port_type = PMAC_SCC_IRDA;
	else if (device_is_compatible(np, "cobalt"))
		modem = 1;
	else if (slots && slots->count > 0) {
		if (strcmp(slots->name, "IrDA") == 0)
			port_type = PMAC_SCC_IRDA;
		else if (strcmp(slots->name, "Modem") == 0)
			modem = 1;
	}
	if (modem)
		pmac_call_feature(PMAC_FTR_MODEM_ENABLE, np, 0, 0);
	pmac_call_feature(PMAC_FTR_SCC_ENABLE, np, port_type, 0);
}

static void __init
set_initial_features(void)
{
	struct device_node* np;
	
	/* That hack appears to be necessary for some StarMax motherboards
	 * but I'm not too sure it was audited for side-effects on other
	 * ohare based machines...
	 * Since I still have difficulties figuring the right way to
	 * differenciate them all and since that hack was there for a long
	 * time, I'll keep it around
	 */
	if (macio_chips[0].type == macio_ohare && !find_devices("via-pmu")) {
		struct macio_chip* macio = &macio_chips[0];
		MACIO_OUT32(OHARE_FCR, STARMAX_FEATURES);
	} else if (macio_chips[0].type == macio_ohare) {
		struct macio_chip* macio = &macio_chips[0];
		MACIO_BIS(OHARE_FCR, OH_IOBUS_ENABLE);
	} else if (macio_chips[1].type == macio_ohare) {
		struct macio_chip* macio = &macio_chips[1];
		MACIO_BIS(OHARE_FCR, OH_IOBUS_ENABLE);
	}

	if (macio_chips[0].type == macio_keylargo ||
	    macio_chips[0].type == macio_pangea) {
		/* Enable GMAC for now for PCI probing. It will be disabled
		 * later on after PCI probe
		 */
		np = find_devices("ethernet");
		while(np) {
			if (np->parent
			    && device_is_compatible(np->parent, "uni-north")
			    && device_is_compatible(np, "gmac"))
				core99_gmac_enable(np, 0, 1);
			np = np->next;
		}

		/* Enable FW before PCI probe. Will be disabled later on
		 * Note: We should have a batter way to check that we are
		 * dealing with uninorth internal cell and not a PCI cell
		 * on the external PCI. The code below works though.
		 */
		np = find_devices("firewire");
		while(np) {
			if (np->parent
			    && device_is_compatible(np->parent, "uni-north")
			    && (device_is_compatible(np, "pci106b,18") || 
	     		        device_is_compatible(np, "pci106b,30") ||
	     		        device_is_compatible(np, "pci11c1,5811"))) {
				macio_chips[0].flags |= MACIO_FLAG_FW_SUPPORTED;
				core99_firewire_enable(np, 0, 1);
			}
			np = np->next;
		}
		
		/* Switch airport off */
		np = find_devices("radio");
		while(np) {
			if (np && np->parent == macio_chips[0].of_node) {
				macio_chips[0].flags |= MACIO_FLAG_AIRPORT_ON;
				core99_airport_enable(np, 0, 0);
			}
			np = np->next;
		}
	}

	/* On all machines, switch sound off */
	if (macio_chips[0].of_node)
		pmac_do_feature_call(PMAC_FTR_SOUND_CHIP_ENABLE,
			macio_chips[0].of_node, 0, 0);

	/* On all machines, switch modem & serial ports off */
	np = find_devices("ch-a");
	while(np) {
		initial_serial_shutdown(np);
		np = np->next;
	}
	np = find_devices("ch-b");
	while(np) {
		initial_serial_shutdown(np);
		np = np->next;
	}
	
	/* Let hardware settle down */
	mdelay(10);
}

void __init
pmac_feature_init(void)
{
	/* Detect the UniNorth memory controller */
	probe_uninorth();

	/* Probe mac-io controllers */
	if (probe_macios()) {
		printk(KERN_WARNING "No mac-io chip found\n");
		return;
	}

	/* Probe machine type */
	if (probe_motherboard())
		printk(KERN_WARNING "Unknown PowerMac !\n");

	/* Set some initial features (turn off some chips that will
	 * be later turned on)
	 */
	set_initial_features();
}

void __init
pmac_feature_late_init(void)
{
	struct device_node* np;
	
	/* Request some resources late */
	if (uninorth_node)
		request_OF_resource(uninorth_node, 0, NULL);
	np = find_devices("hammerhead");
	if (np)
		request_OF_resource(np, 0, NULL);
	np = find_devices("interrupt-controller");
	if (np)
		request_OF_resource(np, 0, NULL);
}
