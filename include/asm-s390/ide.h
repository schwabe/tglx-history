/*
 *  linux/include/asm-s390/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/* s390 does not have IDE */

#ifndef __ASMS390_IDE_H
#define __ASMS390_IDE_H

#ifdef __KERNEL__

#ifndef MAX_HWIFS
#define MAX_HWIFS	0
#endif

#define ide__sti()	do {} while (0)

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(hwif)		(1)
#define ide_fix_driveid(id)		do {} while (0)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

/*
 * We always use the new IDE port registering,
 * so these are fixed here.
 */
#define ide_default_io_base(i)		((ide_ioreg_t)0)
#define ide_default_irq(b)		(0)

#endif /* __KERNEL__ */

#endif /* __ASMS390_IDE_H */
