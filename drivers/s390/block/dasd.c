/*
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * $Revision: 1.71 $
 *
 * History of changes (starts July 2000)
 * 11/09/00 complete redesign after code review
 * 02/01/01 added dynamic registration of ioctls
 *	    fixed bug in registration of new majors
 *	    fixed handling of request during dasd_end_request
 *	    fixed handling of plugged queues
 *	    fixed partition handling and HDIO_GETGEO
 *	    fixed traditional naming scheme for devices beyond 702
 *	    fixed some race conditions related to modules
 *	    added devfs suupport
 * 03/06/01 refined dynamic attach/detach for leaving devices which are online.
 * 03/09/01 refined dynamic modifiaction of devices
 * 03/12/01 moved policy in dasd_format to dasdfmt (renamed BIODASDFORMAT)
 * 03/19/01 added BIODASDINFO-ioctl
 *	    removed 2.2 compatibility
 * 04/27/01 fixed PL030119COT (dasd_disciplines does not work)
 * 04/30/01 fixed PL030146HSM (module locking with dynamic ioctls)
 *	    fixed PL030130SBA (handling of invalid ranges)
 * 05/02/01 fixed PL030145SBA (killing dasdmt)
 *	    fixed PL030149SBA (status of 'accepted' devices)
 *	    fixed PL030146SBA (BUG in ibm.c after adding device)
 *	    added BIODASDPRRD ioctl interface
 * 05/11/01 fixed  PL030164MVE (trap in probeonly mode)
 * 05/15/01 fixed devfs support for unformatted devices
 * 06/26/01 hopefully fixed PL030172SBA,PL030234SBA
 * 07/09/01 fixed PL030324MSH (wrong statistics output)
 * 07/16/01 merged in new fixes for handling low-mem situations
 * 01/22/01 fixed PL030579KBE (wrong statistics)
 * 05/04/02 code restructuring.
 */

#define LOCAL_END_REQUEST /* Don't generate end_request in blk.h */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>

#include <asm/ccwdev.h>
#include <asm/ebcdic.h>
#include <asm/idals.h>
#include <asm/todclk.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd:"

#include "dasd_int.h"
/*
 * SECTION: Constant definitions to be used within this file
 */
#define DASD_CHANQ_MAX_SIZE 5

/*
 * SECTION: exported variables of dasd.c
 */
debug_info_t *dasd_debug_area;

MODULE_AUTHOR("Holger Smolinski <Holger.Smolinski@de.ibm.com>");
MODULE_DESCRIPTION("Linux on S/390 DASD device driver,"
		   " Copyright 2000 IBM Corporation");
MODULE_SUPPORTED_DEVICE("dasd");
MODULE_PARM(dasd, "1-" __MODULE_STRING(256) "s");
MODULE_PARM(dasd_disciplines, "1-" __MODULE_STRING(8) "s");
MODULE_LICENSE("GPL");

/*
 * SECTION: prototypes for static functions of dasd.c
 */
static int  dasd_setup_blkdev(dasd_device_t * device);
static void dasd_disable_blkdev(dasd_device_t * device);
static void dasd_flush_request_queue(dasd_device_t *);
static void dasd_int_handler(struct ccw_device *, unsigned long, struct irb *);
static void dasd_flush_ccw_queue(dasd_device_t *, int);
static void dasd_tasklet(dasd_device_t *);
static void do_kick_device(void *data);
static int  dasd_add_sysfs_files(struct ccw_device *cdev);

/*
 * SECTION: Operations on the device structure.
 */
static wait_queue_head_t dasd_init_waitq;

/*
 * Allocate memory for a new device structure.
 */
dasd_device_t *
dasd_alloc_device(dasd_devmap_t *devmap)
{
	dasd_device_t *device;
	struct gendisk *gdp;

	device = kmalloc(sizeof (dasd_device_t), GFP_ATOMIC);
	if (device == NULL)
		return ERR_PTR(-ENOMEM);
	memset(device, 0, sizeof (dasd_device_t));

	device->devno = devmap->devno;

	/* Get two pages for normal block device operations. */
	device->ccw_mem = (void *) __get_free_pages(GFP_ATOMIC | GFP_DMA, 1);
	if (device->ccw_mem == NULL) {
		kfree(device);
		return ERR_PTR(-ENOMEM);
	}
	/* Get one page for error recovery. */
	device->erp_mem = (void *) get_zeroed_page(GFP_ATOMIC | GFP_DMA);
	if (device->erp_mem == NULL) {
		free_pages((unsigned long) device->ccw_mem, 1);
		kfree(device);
		return ERR_PTR(-ENOMEM);
	}

	/* Allocate gendisk structure for device. */
	gdp = dasd_gendisk_alloc(devmap->devindex);
	if (IS_ERR(gdp)) {
		free_page((unsigned long) device->erp_mem);
		free_pages((unsigned long) device->ccw_mem, 1);
		kfree(device);
		return (dasd_device_t *) gdp;
	}
	gdp->private_data = device;
	device->gdp = gdp;

	dasd_init_chunklist(&device->ccw_chunks, device->ccw_mem, PAGE_SIZE*2);
	dasd_init_chunklist(&device->erp_chunks, device->erp_mem, PAGE_SIZE);
	spin_lock_init(&device->request_queue_lock);
	atomic_set (&device->tasklet_scheduled, 0);
	tasklet_init(&device->tasklet, 
		     (void (*)(unsigned long)) dasd_tasklet,
		     (unsigned long) device);
	INIT_LIST_HEAD(&device->ccw_queue);
	init_timer(&device->timer);
	INIT_WORK(&device->kick_work, do_kick_device, (void *) (addr_t) device->devno);
	device->state = DASD_STATE_NEW;
	device->target = DASD_STATE_NEW;

	return device;
}

/*
 * Free memory of a device structure.
 */
void
dasd_free_device(dasd_device_t *device)
{
	if (device->private)
		kfree(device->private);
	free_page((unsigned long) device->erp_mem);
	free_pages((unsigned long) device->ccw_mem, 1);
	put_disk(device->gdp);
	kfree(device);
}

/*
 * Make a new device known to the system.
 */
static inline int
dasd_state_new_to_known(dasd_device_t *device)
{
	char buffer[10];
	dasd_devmap_t *devmap;
	umode_t devfs_perm;
	devfs_handle_t dir;
	int major, minor;

	/* Increase reference count of bdev. */
	if (bdget(MKDEV(device->gdp->major, device->gdp->first_minor)) == NULL)
		return -ENODEV;

	devmap = dasd_devmap_from_devno(device->devno);
	if (devmap == NULL)
		return -ENODEV;
	major = dasd_gendisk_index_major(devmap->devindex);
	if (major < 0)
		return -ENODEV;
	minor = devmap->devindex % DASD_PER_MAJOR;

	/* Add a proc directory and the dasd device entry to devfs. */
 	sprintf(buffer, "dasd/%04x", device->devno);
 	dir = devfs_mk_dir(NULL, buffer, NULL);
	device->gdp->de = dir;
	if (device->ro_flag)
		devfs_perm = S_IFBLK | S_IRUSR;
	else
		devfs_perm = S_IFBLK | S_IRUSR | S_IWUSR;
	device->devfs_entry = devfs_register(dir, "device", DEVFS_FL_DEFAULT,
					     major, minor << DASD_PARTN_BITS,
					     devfs_perm,
					     &dasd_device_operations, NULL);
	device->state = DASD_STATE_KNOWN;
	return 0;
}

/*
 * Let the system forget about a device.
 */
static inline void
dasd_state_known_to_new(dasd_device_t * device)
{
	struct block_device *bdev;

	/* Remove device entry and devfs directory. */
	devfs_unregister(device->devfs_entry);
	devfs_unregister(device->gdp->de);

	/* Forget the discipline information. */
	device->discipline = NULL;
	device->state = DASD_STATE_NEW;

	/* Decrease reference count of bdev. */
	bdev = bdget(MKDEV(device->gdp->major, device->gdp->first_minor));
	bdput(bdev);
	bdput(bdev);
}

/*
 * Request the irq line for the device.
 */
static inline int
dasd_state_known_to_basic(dasd_device_t * device)
{
	/* register 'device' debug area, used for all DBF_DEV_XXX calls */
	device->debug_area = debug_register(device->gdp->disk_name, 0, 2,
					    8 * sizeof (long));
	debug_register_view(device->debug_area, &debug_sprintf_view);
	debug_set_level(device->debug_area, DBF_ERR);
	DBF_DEV_EVENT(DBF_EMERG, device, "%s", "debug area created");

	device->state = DASD_STATE_BASIC;
	return 0;
}

/*
 * Release the irq line for the device. Terminate any running i/o.
 */
static inline void
dasd_state_basic_to_known(dasd_device_t * device)
{
	dasd_flush_ccw_queue(device, 1);
	DBF_DEV_EVENT(DBF_EMERG, device, "%p debug area deleted", device);
	if (device->debug_area != NULL) {
		debug_unregister(device->debug_area);
		device->debug_area = NULL;
	}
	device->state = DASD_STATE_KNOWN;
}

/*
 * Do the initial analysis. The do_analysis function may return
 * -EAGAIN in which case the device keeps the state DASD_STATE_BASIC
 * until the discipline decides to continue the startup sequence
 * by calling the function dasd_change_state. The eckd disciplines
 * uses this to start a ccw that detects the format. The completion
 * interrupt for this detection ccw uses the kernel event daemon to
 * trigger the call to dasd_change_state. All this is done in the
 * discipline code, see dasd_eckd.c.
 */
static inline int
dasd_state_basic_to_accept(dasd_device_t * device)
{
	int rc;

	rc = 0;
	if (device->discipline->do_analysis != NULL)
		rc = device->discipline->do_analysis(device);
	if (rc == 0)
		device->state = DASD_STATE_ACCEPT;
	return rc;
}

/*
 * Forget everything the initial analysis found out.
 */
static inline void
dasd_state_accept_to_basic(dasd_device_t * device)
{
	device->blocks = 0;
	device->bp_block = 0;
	device->s2b_shift = 0;
	device->state = DASD_STATE_BASIC;
}

/*
 * Setup block device.
 */
static inline int
dasd_state_accept_to_ready(dasd_device_t * device)
{
	int rc;

	rc = dasd_setup_blkdev(device);
	if (rc == 0) {
		dasd_setup_partitions(device);
		device->state = DASD_STATE_READY;
	}
	return rc;
}

/*
 * Remove device from block device layer. Destroy dirty buffers.
 */
static inline void
dasd_state_ready_to_accept(dasd_device_t * device)
{
	dasd_flush_ccw_queue(device, 0);
	dasd_destroy_partitions(device);
	dasd_flush_request_queue(device);
	dasd_disable_blkdev(device);
	device->state = DASD_STATE_ACCEPT;
}

/*
 * Make the device online and schedule the bottom half to start
 * the requeueing of requests from the linux request queue to the
 * ccw queue.
 */
static inline int
dasd_state_ready_to_online(dasd_device_t * device)
{
	device->state = DASD_STATE_ONLINE;
	dasd_schedule_bh(device);
	return 0;
}

/*
 * Stop the requeueing of requests again.
 */
static inline void
dasd_state_online_to_ready(dasd_device_t * device)
{
	device->state = DASD_STATE_READY;
}

/*
 * Device startup state changes.
 */
static inline int
dasd_increase_state(dasd_device_t *device)
{
	int rc;

	rc = 0;
	if (device->state == DASD_STATE_NEW &&
	    device->target >= DASD_STATE_KNOWN)
		rc = dasd_state_new_to_known(device);

	if (device->state == DASD_STATE_KNOWN &&
	    device->target >= DASD_STATE_BASIC)
		rc = dasd_state_known_to_basic(device);

	if (device->state == DASD_STATE_BASIC &&
	    device->target >= DASD_STATE_ACCEPT)
		rc = dasd_state_basic_to_accept(device);

	if (device->state == DASD_STATE_ACCEPT &&
	    device->target >= DASD_STATE_READY)
		rc = dasd_state_accept_to_ready(device);

	if (device->state == DASD_STATE_READY &&
	    device->target >= DASD_STATE_ONLINE)
		rc = dasd_state_ready_to_online(device);

	return rc;
}

/*
 * Device shutdown state changes.
 */
static inline int
dasd_decrease_state(dasd_device_t *device)
{
	if (device->state == DASD_STATE_ONLINE &&
	    device->target <= DASD_STATE_READY)
		dasd_state_online_to_ready(device);
	
	if (device->state == DASD_STATE_READY &&
	    device->target <= DASD_STATE_ACCEPT)
		dasd_state_ready_to_accept(device);
	
	if (device->state == DASD_STATE_ACCEPT && 
	    device->target <= DASD_STATE_BASIC)
		dasd_state_accept_to_basic(device);
	
	if (device->state == DASD_STATE_BASIC && 
	    device->target <= DASD_STATE_KNOWN)
		dasd_state_basic_to_known(device);
	
	if (device->state == DASD_STATE_KNOWN &&
	    device->target <= DASD_STATE_NEW)
		dasd_state_known_to_new(device);

	return 0;
}

/*
 * This is the main startup/shutdown routine.
 */
static void
dasd_change_state(dasd_device_t *device)
{
        int rc;

	if (device->state == device->target)
		/* Already where we want to go today... */
		return;
	if (device->state < device->target)
		rc = dasd_increase_state(device);
	else
		rc = dasd_decrease_state(device);
        if (rc && rc != -EAGAIN) {
		if (rc != -ENODEV)
			MESSAGE (KERN_INFO, "giving up on dasd device with "
				 "devno %04x", device->devno);
                device->target = device->state;
        }

	if (device->state == device->target)
		wake_up(&dasd_init_waitq);
}

/*
 * Kick starter for devices that did not complete the startup/shutdown
 * procedure or were sleeping because of a pending state.
 * dasd_kick_device will schedule a call do do_kick_device to the kernel
 * event daemon.
 */
static void
do_kick_device(void *data)
{
	int devno;
	dasd_devmap_t *devmap;
	dasd_device_t *device;

	devno = (long) data;
	devmap = dasd_devmap_from_devno(devno);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return;
	atomic_dec(&device->ref_count);
	dasd_change_state(device);
	dasd_schedule_bh(device);
	dasd_put_device(devmap);
}

void
dasd_kick_device(dasd_device_t *device)
{
	atomic_inc(&device->ref_count);
	/* queue call to dasd_kick_device to the kernel event daemon. */
	schedule_work(&device->kick_work);
}

/*
 * Set the target state for a device and starts the state change.
 */
void
dasd_set_target_state(dasd_device_t *device, int target)
{
	/* If we are in probeonly mode stop at DASD_STATE_ACCEPT. */
	if (dasd_probeonly && target > DASD_STATE_ACCEPT)
		target = DASD_STATE_ACCEPT;
	if (device->target != target) {
                if (device->state == target)
                        wake_up(&dasd_init_waitq);
		device->target = target;
	}
	if (device->state != device->target)
		dasd_change_state(device);
}

/*
 * Enable devices with device numbers in [from..to].
 */
static inline int
_wait_for_device(dasd_device_t *device)
{
	return (device->state == device->target);
}

// FIXME: if called from dasd_devices_write discpline is not set -> oops.
void
dasd_enable_device(dasd_device_t *device)
{
	dasd_set_target_state(device, DASD_STATE_ONLINE);
	if (device->state <= DASD_STATE_KNOWN)
		/* No discipline for device found. */
		dasd_set_target_state(device, DASD_STATE_NEW);
	/* Now wait for the devices to come up. */
	wait_event(dasd_init_waitq, _wait_for_device(device));
}

/*
 * SECTION: device operation (interrupt handler, start i/o, term i/o ...)
 */
#ifdef CONFIG_DASD_PROFILE

dasd_profile_info_t dasd_global_profile;
unsigned int dasd_profile_level = DASD_PROFILE_OFF;

/*
 * Increments counter in global and local profiling structures.
 */
#define dasd_profile_counter(value, counter, device) \
{ \
	int index; \
	for (index = 0; index < 31 && value >> (2+index); index++); \
	dasd_global_profile.counter[index]++; \
	device->profile.counter[index]++; \
}

/*
 * Add profiling information for cqr before execution.
 */
static inline void
dasd_profile_start(dasd_device_t *device, dasd_ccw_req_t * cqr,
		   struct request *req)
{
	struct list_head *l;
	unsigned int counter;

	if (dasd_profile_level != DASD_PROFILE_ON)
		return;

	/* count the length of the chanq for statistics */
	counter = 0;
	list_for_each(l, &device->ccw_queue)
		if (++counter >= 31)
			break;
	dasd_global_profile.dasd_io_nr_req[counter]++;
	device->profile.dasd_io_nr_req[counter]++;
}

/*
 * Add profiling information for cqr after execution.
 */
static inline void
dasd_profile_end(dasd_device_t *device, dasd_ccw_req_t * cqr,
		 struct request *req)
{
	long strtime, irqtime, endtime, tottime;	/* in microsecnds */
	long tottimeps, sectors;

	if (dasd_profile_level != DASD_PROFILE_ON)
		return;

	sectors = req->nr_sectors;
	if (!cqr->buildclk || !cqr->startclk ||
	    !cqr->stopclk || !cqr->endclk ||
	    !sectors)
		return;

	strtime = ((cqr->startclk - cqr->buildclk) >> 12);
	irqtime = ((cqr->stopclk - cqr->startclk) >> 12);
	endtime = ((cqr->endclk - cqr->stopclk) >> 12);
	tottime = ((cqr->endclk - cqr->buildclk) >> 12);
	tottimeps = tottime / sectors;

	if (!dasd_global_profile.dasd_io_reqs)
		memset(&dasd_global_profile, 0, sizeof (dasd_profile_info_t));
	dasd_global_profile.dasd_io_reqs++;
	dasd_global_profile.dasd_io_sects += sectors;

	if (!device->profile.dasd_io_reqs)
		memset(&device->profile, 0, sizeof (dasd_profile_info_t));
	device->profile.dasd_io_reqs++;
	device->profile.dasd_io_sects += sectors;

	dasd_profile_counter(sectors, dasd_io_secs, device);
	dasd_profile_counter(tottime, dasd_io_times, device);
	dasd_profile_counter(tottimeps, dasd_io_timps, device);
	dasd_profile_counter(strtime, dasd_io_time1, device);
	dasd_profile_counter(irqtime, dasd_io_time2, device);
	dasd_profile_counter(irqtime / sectors, dasd_io_time2ps, device);
	dasd_profile_counter(endtime, dasd_io_time3, device);
}
#else
#define dasd_profile_start(device, cqr, req) do {} while (0)
#define dasd_profile_end(device, cqr, req) do {} while (0)
#endif				/* CONFIG_DASD_PROFILE */

/*
 * Allocate memory for a channel program with 'cplength' channel
 * command words and 'datasize' additional space. There are two
 * variantes: 1) dasd_kmalloc_request uses kmalloc to get the needed
 * memory and 2) dasd_smalloc_request uses the static ccw memory
 * that gets allocated for each device.
 */
dasd_ccw_req_t *
dasd_kmalloc_request(char *magic, int cplength, int datasize,
		   dasd_device_t * device)
{
	dasd_ccw_req_t *cqr;

	/* Sanity checks */
	if ( magic == NULL || datasize > PAGE_SIZE ||
	     (cplength*sizeof(struct ccw1)) > PAGE_SIZE)
		BUG();
	debug_text_event ( dasd_debug_area, 1, "ALLC");
	debug_text_event ( dasd_debug_area, 1, magic);
	debug_int_event ( dasd_debug_area, 1, cplength);
	debug_int_event ( dasd_debug_area, 1, datasize);

	cqr = kmalloc(sizeof(dasd_ccw_req_t), GFP_ATOMIC);
	if (cqr == NULL)
		return ERR_PTR(-ENOMEM);
	memset(cqr, 0, sizeof(dasd_ccw_req_t));
	cqr->cpaddr = NULL;
	if (cplength > 0) {
		cqr->cpaddr = kmalloc(cplength*sizeof(struct ccw1),
				      GFP_ATOMIC | GFP_DMA);
		if (cqr->cpaddr == NULL) {
			kfree(cqr);
			return ERR_PTR(-ENOMEM);
		}
		memset(cqr->cpaddr, 0, cplength*sizeof(struct ccw1));
	}
	cqr->data = NULL;
	if (datasize > 0) {
		cqr->data = kmalloc(datasize, GFP_ATOMIC | GFP_DMA);
		if (cqr->data == NULL) {
			if (cqr->cpaddr != NULL)
				kfree(cqr->cpaddr);
			kfree(cqr);
			return ERR_PTR(-ENOMEM);
		}
		memset(cqr->data, 0, datasize);
	}
	strncpy((char *) &cqr->magic, magic, 4);
	ASCEBC((char *) &cqr->magic, 4);
	atomic_inc(&device->ref_count);
	return cqr;
}

dasd_ccw_req_t *
dasd_smalloc_request(char *magic, int cplength, int datasize,
		   dasd_device_t * device)
{
	unsigned long flags;
	dasd_ccw_req_t *cqr;
	char *data;
	int size;

	/* Sanity checks */
	if ( magic == NULL || datasize > PAGE_SIZE ||
	     (cplength*sizeof(struct ccw1)) > PAGE_SIZE)
		BUG();
	debug_text_event ( dasd_debug_area, 1, "ALLC");
	debug_text_event ( dasd_debug_area, 1, magic);
	debug_int_event ( dasd_debug_area, 1, cplength);
	debug_int_event ( dasd_debug_area, 1, datasize);

	size = (sizeof(dasd_ccw_req_t) + 7L) & -8L;
	if (cplength > 0)
		size += cplength * sizeof(struct ccw1);
	if (datasize > 0)
		size += datasize;
	spin_lock_irqsave(&device->mem_lock, flags);
	cqr = (dasd_ccw_req_t *) dasd_alloc_chunk(&device->ccw_chunks, size);
	spin_unlock_irqrestore(&device->mem_lock, flags);
	if (cqr == NULL)
		return ERR_PTR(-ENOMEM);
	memset(cqr, 0, sizeof(dasd_ccw_req_t));
	data = (char *) cqr + ((sizeof(dasd_ccw_req_t) + 7L) & -8L);
	cqr->cpaddr = NULL;
	if (cplength > 0) {
		cqr->cpaddr = (struct ccw1 *) data;
		data += cplength*sizeof(struct ccw1);
		memset(cqr->cpaddr, 0, cplength*sizeof(struct ccw1));
	}
	cqr->data = NULL;
	if (datasize > 0) {
		cqr->data = data;
 		memset(cqr->data, 0, datasize);
	}
	strncpy((char *) &cqr->magic, magic, 4);
	ASCEBC((char *) &cqr->magic, 4);
	atomic_inc(&device->ref_count);
	return cqr;
}

/*
 * Free memory of a channel program. This function needs to free all the
 * idal lists that might have been created by dasd_set_cda and the
 * dasd_ccw_req_t itself.
 */
void
dasd_kfree_request(dasd_ccw_req_t * cqr, dasd_device_t * device)
{
#ifdef CONFIG_ARCH_S390X
	struct ccw1 *ccw;

	/* Clear any idals used for the request. */
	ccw = cqr->cpaddr;
	do {
		clear_normalized_cda(ccw);
	} while (ccw++->flags & (CCW_FLAG_CC | CCW_FLAG_DC));
#endif
	if (cqr->dstat != NULL)
		kfree(cqr->dstat);
	debug_text_event ( dasd_debug_area, 1, "FREE");
	debug_int_event ( dasd_debug_area, 1, (long) cqr);
	if (cqr->cpaddr != NULL)
		kfree(cqr->cpaddr);
	if (cqr->data != NULL)
		kfree(cqr->data);
	kfree(cqr);
	atomic_dec(&device->ref_count);
}

void
dasd_sfree_request(dasd_ccw_req_t * cqr, dasd_device_t * device)
{
	unsigned long flags;

	if (cqr->dstat != NULL)
		kfree(cqr->dstat);
	debug_text_event(dasd_debug_area, 1, "FREE");
	debug_int_event(dasd_debug_area, 1, (long) cqr);
	spin_lock_irqsave(&device->mem_lock, flags);
	dasd_free_chunk(&device->ccw_chunks, cqr);
	spin_unlock_irqrestore(&device->mem_lock, flags);
	atomic_dec(&device->ref_count);
}

/*
 * Check discipline magic in cqr.
 */
static inline int
dasd_check_cqr(dasd_ccw_req_t *cqr)
{
	dasd_device_t *device;

	if (cqr == NULL)
		return -EINVAL;
	device = cqr->device;
	if (strncmp((char *) &cqr->magic, device->discipline->ebcname, 4)) {
		DEV_MESSAGE(KERN_WARNING, device,
			    " dasd_ccw_req_t 0x%08x magic doesn't match"
			    " discipline 0x%08x",
			    cqr->magic,
			    *(unsigned int *) device->discipline->name);
		return -EINVAL;
	}
	return 0;
}

/*
 * Terminate the current i/o and set the request to failed.
 * ccw_device_halt/ccw_device_clear can fail if the i/o subsystem 
 * is in a bad mood.
 */
int
dasd_term_IO(dasd_ccw_req_t * cqr)
{
	dasd_device_t *device;
	int retries, rc;

	/* Check the cqr */
	rc = dasd_check_cqr(cqr);
	if (rc)
		return rc;
	retries = 0;
	device = (dasd_device_t *) cqr->device;
	while ((retries < 5) && (cqr->status == DASD_CQR_IN_IO)) {
		if (retries < 2)
			rc = ccw_device_halt(device->cdev, (long) cqr);
		else
			rc = ccw_device_clear(device->cdev, (long) cqr);
		switch (rc) {
		case 0:	/* termination successful */
			cqr->status = DASD_CQR_FAILED;
			cqr->stopclk = get_clock();
			break;
		case -ENODEV:
			DBF_DEV_EVENT(DBF_ERR, device, "%s",
				      "device gone, retry");
			break;
		case -EIO:
			DBF_DEV_EVENT(DBF_ERR, device, "%s",
				      "I/O error, retry");
			break;
		case -EBUSY:
			DBF_DEV_EVENT(DBF_ERR, device, "%s",
				      "device busy, retry later");
			break;
		default:
			DEV_MESSAGE(KERN_ERR, device,
				    "line %d unknown RC=%d, please "
				    "report to linux390@de.ibm.com",
				    __LINE__, rc);
			BUG();
			break;
		}
		retries++;
	}
	dasd_schedule_bh(device);
	return rc;
}

/*
 * Start the i/o. This start_IO can fail if the channel is really busy.
 * In that case set up a timer to start the request later.
 */
int
dasd_start_IO(dasd_ccw_req_t * cqr)
{
	dasd_device_t *device;
	int rc;

	/* Check the cqr */
	rc = dasd_check_cqr(cqr);
	if (rc)
		return rc;
	device = (dasd_device_t *) cqr->device;
	cqr->startclk = get_clock();
	rc = ccw_device_start(device->cdev, cqr->cpaddr, (long) cqr,
			      cqr->lpm, 0);
	switch (rc) {
	case 0:
		cqr->status = DASD_CQR_IN_IO;
		break;
	case -EBUSY:
		DBF_DEV_EVENT(DBF_ERR, device, "%s",
			      "device busy, retry later");
		break;
	case -ETIMEDOUT:
		DBF_DEV_EVENT(DBF_ERR, device, "%s",
			      "request timeout - terminated");
	case -ENODEV:
	case -EIO:
		cqr->status = DASD_CQR_FAILED;
		cqr->stopclk = cqr->startclk;
		dasd_schedule_bh(device);
		break;
	default:
		DEV_MESSAGE(KERN_ERR, device,
			    "line %d unknown RC=%d, please report"
			    " to linux390@de.ibm.com", __LINE__, rc);
		BUG();
		break;
	}
	return rc;
}

/*
 * Timeout function for dasd devices. This is used for different purposes
 *  1) missing interrupt handler for normal operation
 *  2) delayed start of request where start_IO failed with -EBUSY
 *  3) timeout for missing state change interrupts
 * The head of the ccw queue will have status DASD_CQR_IN_IO for 1),
 * DASD_CQR_QUEUED for 2) and DASD_CQR_PENDING for 3).
 */
static void
dasd_timeout_device(unsigned long ptr)
{
	unsigned long flags;
	dasd_device_t *device;
	dasd_ccw_req_t *cqr;

	device = (dasd_device_t *) ptr;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	/* re-activate first request in queue */
	if (!list_empty(&device->ccw_queue)) {
		cqr = list_entry(device->ccw_queue.next, dasd_ccw_req_t, list);
		if (cqr->status == DASD_CQR_PENDING)
			cqr->status = DASD_CQR_QUEUED;
	}
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	dasd_schedule_bh(device);
}

/*
 * Setup timeout for a device.
 */
void
dasd_set_timer(dasd_device_t *device, int expires)
{
	/* FIXME: timeouts are based on jiffies but the timeout
	 * comparision in __dasd_check_expire is based on the
	 * TOD clock. */
	if (expires == 0) {
		if (timer_pending(&device->timer))
			del_timer(&device->timer);
		return;
	}
	if (timer_pending(&device->timer)) {
		if (mod_timer(&device->timer, jiffies + expires))
			return;
	}
	device->timer.function = dasd_timeout_device;
	device->timer.data = (unsigned long) device;
	device->timer.expires = jiffies + expires;
	add_timer(&device->timer);
}

/*
 * Clear timeout for a device.
 */
void
dasd_clear_timer(dasd_device_t *device)
{
	if (timer_pending(&device->timer))
		del_timer(&device->timer);
}

/*
 *   Handles the state change pending interrupt.
 *   Search for the device related request queue and check if the first
 *   cqr in queue in in status 'DASD_CQR_PENDING'.
 *   If so the status is set to 'DASD_CQR_QUEUED' to reactivate
 *   the device.
 */
static void
do_state_change_pending(void *data)
{
	struct {
		struct work_struct work;
		dasd_device_t *device;
	} *p;
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	dasd_ccw_req_t *cqr;
	int devno;

	p = data;
	device = p->device;
	DBF_EVENT(DBF_NOTICE, "State change Interrupt for bus_id %s",
		  device->cdev->dev.bus_id);

	// FIXME: get rid of devmap.
	devno = _ccw_device_get_device_number(device->cdev);
	devmap = dasd_devmap_from_devno(devno);
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	/* re-activate first request in queue */
	if (!list_empty(&device->ccw_queue)) {
		cqr = list_entry(device->ccw_queue.next, dasd_ccw_req_t, list);
		if (cqr == NULL) {
			MESSAGE (KERN_DEBUG,
				 "got state change pending interrupt on"
				 "an idle device: bus_id %s",
				 device->cdev->dev.bus_id);
			return;
		}
		if (cqr->status == DASD_CQR_PENDING)
			cqr->status = DASD_CQR_QUEUED;
	}
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	dasd_schedule_bh(device);
	dasd_put_device(devmap);
	kfree(p);
}


static void
dasd_handle_state_change_pending(dasd_device_t *device)
{
	struct {
		struct work_struct work;
		dasd_device_t *device;
	} *p;

	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (p == NULL)
		/* No memory, let the timeout do the reactivation. */
		return;
	INIT_WORK(&p->work, (void *) do_state_change_pending, p);
	p->device = device;
	atomic_inc(&device->ref_count);
	/* queue call to do_state_change_pending to the kernel event daemon. */
	schedule_work(&p->work);
}

/*
 * Interrupt handler for "normal" ssch-io based dasd devices.
 */
void
dasd_int_handler(struct ccw_device *cdev, unsigned long intparm,
		 struct irb *irb)
{
	dasd_ccw_req_t *cqr, *next;
	dasd_device_t *device;
	unsigned long long now;
	int expires;
	dasd_era_t era;
	char mask;

	now = get_clock();

	DBF_EVENT(DBF_DEBUG, "Interrupt: stat %02x, bus_id %s",
		  irb->scsw.dstat, cdev->dev.bus_id);

	/* first of all check for state change pending interrupt */
	mask = DEV_STAT_ATTENTION | DEV_STAT_DEV_END | DEV_STAT_UNIT_EXCEP;
	if ((irb->scsw.dstat & mask) == mask) {
		dasd_handle_state_change_pending(cdev->dev.driver_data);
		return;
	}

	cqr = (dasd_ccw_req_t *) intparm;
	/*
	 * check status - the request might have been killed
	 * because of dyn detach
	 */
	if (cqr->status != DASD_CQR_IN_IO) {
		MESSAGE(KERN_DEBUG,
			"invalid status: bus_id %s, status %02x",
			cdev->dev.bus_id, cqr->status);
		return;
	}

	device = (dasd_device_t *) cqr->device;
	if (device == NULL ||
	    device != cdev->dev.driver_data ||
	    strncmp(device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		MESSAGE(KERN_DEBUG, "invalid device in request: bus_id %s",
			cdev->dev.bus_id);
		return;
	}

	DBF_DEV_EVENT(DBF_DEBUG, device, "Int: CS/DS 0x%04x",
		      ((irb->scsw.cstat << 8) | irb->scsw.dstat));

	/* Find out the appropriate era_action. */
	era = dasd_era_none;
	if (irb->scsw.dstat & ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END) ||
	    irb->esw.esw0.erw.cons) {
		/* The request did end abnormally. */
		if (irb->scsw.fctl & SCSW_FCTL_HALT_FUNC)
			era = dasd_era_fatal;
		else
			era = device->discipline->examine_error(cqr, irb);
		DBF_EVENT(DBF_NOTICE, "era_code %d", era);
	}
	expires = 0;
	if (era == dasd_era_none) {
		cqr->status = DASD_CQR_DONE;
		cqr->stopclk = now;
		/* Start first request on queue if possible -> fast_io. */
		if (cqr->list.next != &device->ccw_queue) {
			next = list_entry(cqr->list.next,
					  dasd_ccw_req_t, list);
			if (next->status == DASD_CQR_QUEUED) {
				if (device->discipline->start_IO(next) == 0)
					expires = next->expires;
				else
					MESSAGE(KERN_WARNING, "%s",
						"Interrupt fastpath failed!");
			}
		}
	} else {		/* error */
		if (cqr->dstat == NULL)
			cqr->dstat = kmalloc(sizeof(struct irb), GFP_ATOMIC);
		if (cqr->dstat)
			memcpy(cqr->dstat, irb, sizeof (struct irb));
		else
			MESSAGE(KERN_ERR, "%s",
				"no memory for dstat...ignoring");
#ifdef ERP_DEBUG
		/* dump sense data */
		if (device->discipline && device->discipline->dump_sense)
			device->discipline->dump_sense(device, cqr);
#endif
		switch (era) {
		case dasd_era_fatal:
			cqr->status = DASD_CQR_FAILED;
			cqr->stopclk = now;
			break;
		case dasd_era_recover:
			cqr->status = DASD_CQR_ERROR;
			break;
		default:
			BUG();
		}
	}
	if (expires != 0)
		dasd_set_timer(device, expires);
	else
		dasd_clear_timer(device);
	dasd_schedule_bh(device);
}

/*
 * posts the buffer_cache about a finalized request
 */
static inline void
dasd_end_request(struct request *req, int uptodate)
{
	if (end_that_request_first(req, uptodate, req->hard_nr_sectors))
		BUG();
	add_disk_randomness(req->rq_disk);
	end_that_request_last(req);
	return;
}

/*
 * Process finished error recovery ccw.
 */
static inline void
__dasd_process_erp(dasd_device_t *device, dasd_ccw_req_t *cqr)
{
	dasd_erp_fn_t erp_fn;

	if (cqr->status == DASD_CQR_DONE)
		DBF_DEV_EVENT(DBF_NOTICE, device, "%s", "ERP successful");
	else
		DEV_MESSAGE(KERN_ERR, device, "%s", "ERP unsuccessful");
	erp_fn = device->discipline->erp_postaction(cqr);
	erp_fn(cqr);
}

/*
 * Process ccw request queue.
 */
static inline void
__dasd_process_ccw_queue(dasd_device_t * device, struct list_head *final_queue)
{
	struct list_head *l, *n;
	dasd_ccw_req_t *cqr;
	dasd_erp_fn_t erp_fn;

restart:
	/* Process request with final status. */
	list_for_each_safe(l, n, &device->ccw_queue) {
		cqr = list_entry(l, dasd_ccw_req_t, list);
		/* Stop list processing at the first non-final request. */
		if (cqr->status != DASD_CQR_DONE &&
		    cqr->status != DASD_CQR_FAILED &&
		    cqr->status != DASD_CQR_ERROR)
			break;
		/*  Process requests with DASD_CQR_ERROR */
		if (cqr->status == DASD_CQR_ERROR) {
			cqr->retries--;
			if (cqr->dstat->scsw.fctl & SCSW_FCTL_HALT_FUNC) {
				cqr->status = DASD_CQR_FAILED;
				cqr->stopclk = get_clock();
			} else {
				erp_fn = device->discipline->erp_action(cqr);
				erp_fn(cqr);
			}
			goto restart;
		}
		/* Process finished ERP request. */
		if (cqr->refers) {
			__dasd_process_erp(device, cqr);
			goto restart;
		}

		/* Rechain finished requests to final queue */
		cqr->endclk = get_clock();
		list_move_tail(&cqr->list, final_queue);
	}
}

static void
dasd_end_request_cb(dasd_ccw_req_t * cqr, void *data)
{
	struct request *req;

	req = (struct request *) data;
	dasd_profile_end(cqr->device, cqr, req);
	dasd_end_request(req, (cqr->status == DASD_CQR_DONE));
	dasd_sfree_request(cqr, cqr->device);
}


/*
 * Fetch requests from the block device queue.
 */
static inline void
__dasd_process_blk_queue(dasd_device_t * device)
{
	request_queue_t *queue;
	struct list_head *l;
	struct request *req;
	dasd_ccw_req_t *cqr;
	int nr_queued;

	queue = device->request_queue;
	/* No queue ? Then there is nothing to do. */
	if (queue == NULL)
		return;

	/*
	 * We requeue request from the block device queue to the ccw
	 * queue only in two states. In state DASD_STATE_ACCEPT the
	 * partition detection is done and we need to requeue requests
	 * for that. State DASD_STATE_ONLINE is normal block device
	 * operation.
	 */
	if (device->state != DASD_STATE_ACCEPT &&
	    device->state != DASD_STATE_ONLINE)
		return;
	nr_queued = 0;
	/* Now we try to fetch requests from the request queue */
	list_for_each(l, &device->ccw_queue) {
		cqr = list_entry(l, dasd_ccw_req_t, list);
		if (cqr->status == DASD_CQR_QUEUED)
			nr_queued++;
	}
	while (!blk_queue_plugged(queue) &&
	       !blk_queue_empty(queue) &&
		nr_queued < DASD_CHANQ_MAX_SIZE) {
		req = elv_next_request(queue);
		if (device->ro_flag && rq_data_dir(req) == WRITE) {
			DBF_EVENT(DBF_ERR,
				  "(%04x) Rejecting write request %p",
				  device->devno, req);
			blkdev_dequeue_request(req);
			dasd_end_request(req, 0);
			continue;
		}
		cqr = device->discipline->build_cp(device, req);
		if (IS_ERR(cqr)) {
			if (PTR_ERR(cqr) == -ENOMEM)
				break;	/* terminate request queue loop */
			DBF_EVENT(DBF_ERR,
				  "(%04x) CCW creation failed on request %p",
				  device->devno, req);
			blkdev_dequeue_request(req);
			dasd_end_request(req, 0);
			continue;
		}
		cqr->callback = dasd_end_request_cb;
		cqr->callback_data = (void *) req;
		cqr->status = DASD_CQR_QUEUED;
		blkdev_dequeue_request(req);
		list_add_tail(&cqr->list, &device->ccw_queue);
		dasd_profile_start(device, cqr, req);
		nr_queued++;
	}
}

/*
 * Take a look at the first request on the ccw queue and check
 * if it reached its expire time. If so, terminate the IO.
 */
static inline void
__dasd_check_expire(dasd_device_t * device)
{
	dasd_ccw_req_t *cqr;
	unsigned long long now;

	if (list_empty(&device->ccw_queue))
		return;
	cqr = list_entry(device->ccw_queue.next, dasd_ccw_req_t, list);
	if (cqr->status == DASD_CQR_IN_IO && cqr->expires != 0) {
		now = get_clock();
		if (cqr->expires * (TOD_SEC / HZ) + cqr->startclk < now) {
			if (device->discipline->term_IO(cqr) != 0)
				/* Hmpf, try again in 1/100 sec */
				dasd_set_timer(device, 1);
		}
	}
}

/*
 * Take a look at the first request on the ccw queue and check
 * if it needs to be started.
 */
static inline void
__dasd_start_head(dasd_device_t * device)
{
	dasd_ccw_req_t *cqr;
	int rc;

	if (list_empty(&device->ccw_queue))
		return;
	cqr = list_entry(device->ccw_queue.next, dasd_ccw_req_t, list);
	if (cqr->status == DASD_CQR_QUEUED) {
		/* try to start the first I/O that can be started */
		rc = device->discipline->start_IO(cqr);
		if (rc == 0)
			dasd_set_timer(device, cqr->expires);
		else if (rc == -EBUSY)
				/* Hmpf, try again in 1/100 sec */
			dasd_set_timer(device, 1);
	}
}

/*
 * Remove requests from the ccw queue. 
 */
static void
dasd_flush_ccw_queue(dasd_device_t * device, int all)
{
	struct list_head flush_queue;
	struct list_head *l, *n;
	dasd_ccw_req_t *cqr;

	INIT_LIST_HEAD(&flush_queue);
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	list_for_each_safe(l, n, &device->ccw_queue) {
		cqr = list_entry(l, dasd_ccw_req_t, list);
		/* Flush all request or only block device requests? */
		if (all == 0 && cqr->callback == dasd_end_request_cb)
			continue;
		if (cqr->status == DASD_CQR_IN_IO)
			device->discipline->term_IO(cqr);
		if (cqr->status != DASD_CQR_DONE ||
		    cqr->status != DASD_CQR_FAILED) {
			cqr->status = DASD_CQR_FAILED;
			cqr->stopclk = get_clock();
		}
		/* Process finished ERP request. */
		if (cqr->refers) {
			__dasd_process_erp(device, cqr);
			continue;
		}
		/* Rechain request on device request queue */
		cqr->endclk = get_clock();
		list_move_tail(&cqr->list, &flush_queue);
	}
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	/* Now call the callback function of flushed requests */
	list_for_each_safe(l, n, &flush_queue) {
		cqr = list_entry(l, dasd_ccw_req_t, list);
		if (cqr->callback != NULL)
			(cqr->callback)(cqr, cqr->callback_data);
	}
}

/*
 * Acquire the device lock and process queues for the device.
 */
static void
dasd_tasklet(dasd_device_t * device)
{
	struct list_head final_queue;
	struct list_head *l, *n;
	dasd_ccw_req_t *cqr;

	atomic_set (&device->tasklet_scheduled, 0);
	INIT_LIST_HEAD(&final_queue);
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	/* Check expire time of first request on the ccw queue. */
	__dasd_check_expire(device);
	/* Finish off requests on ccw queue */
	__dasd_process_ccw_queue(device, &final_queue);
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	/* Now call the callback function of requests with final status */
	list_for_each_safe(l, n, &final_queue) {
		cqr = list_entry(l, dasd_ccw_req_t, list);
		list_del(&cqr->list);
		if (cqr->callback != NULL)
			(cqr->callback)(cqr, cqr->callback_data);
	}
	spin_lock_irq(&device->request_queue_lock);
	spin_lock(get_ccwdev_lock(device->cdev));
	/* Get new request from the block device request queue */
	__dasd_process_blk_queue(device);
	/* Now check if the head of the ccw queue needs to be started. */
	__dasd_start_head(device);
	spin_unlock(get_ccwdev_lock(device->cdev));
	spin_unlock_irq(&device->request_queue_lock);
	/* FIXME: what if ref_count == 0 && state == DASD_STATE_NEW ?? */
	atomic_dec(&device->ref_count);
}

/*
 * Schedules a call to dasd_tasklet over the device tasklet.
 */
void
dasd_schedule_bh(dasd_device_t * device)
{
	/* Protect against rescheduling. */
	if (atomic_compare_and_swap (0, 1, &device->tasklet_scheduled))
		return;
	atomic_inc(&device->ref_count);
	tasklet_hi_schedule(&device->tasklet);
}

/*
 * Queue a request to the head of the ccw_queue. Start the I/O if
 * possible.
 */
void
dasd_add_request_head(dasd_ccw_req_t *req)
{
	dasd_device_t *device;
	unsigned long flags;

	device = req->device;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	req->status = DASD_CQR_QUEUED;
	req->device = device;
	list_add(&req->list, &device->ccw_queue);
	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
}

/*
 * Queue a request to the tail of the ccw_queue. Start the I/O if
 * possible.
 */
void
dasd_add_request_tail(dasd_ccw_req_t *req)
{
	dasd_device_t *device;
	unsigned long flags;

	device = req->device;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	req->status = DASD_CQR_QUEUED;
	req->device = device;
	list_add_tail(&req->list, &device->ccw_queue);
	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
}

/*
 * Wakeup callback.
 */
static void
dasd_wakeup_cb(dasd_ccw_req_t *cqr, void *data)
{
	wake_up((wait_queue_head_t *) data);
}

static inline int
_wait_for_wakeup(dasd_ccw_req_t *cqr)
{
	dasd_device_t *device;
	int rc;

	device = cqr->device;
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	rc = cqr->status == DASD_CQR_DONE || cqr->status == DASD_CQR_FAILED;
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	return rc;
}

/*
 * Attempts to start a special ccw queue and waits for its completion.
 */
int
dasd_sleep_on(dasd_ccw_req_t * cqr)
{
	wait_queue_head_t wait_q;
	dasd_device_t *device;
	int rc;
	
	device = cqr->device;
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	
	init_waitqueue_head (&wait_q);
	cqr->callback = dasd_wakeup_cb;
	cqr->callback_data = (void *) &wait_q;
	cqr->status = DASD_CQR_QUEUED;
	list_add_tail(&cqr->list, &device->ccw_queue);
	
	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);
	
	spin_unlock_irq(get_ccwdev_lock(device->cdev));

	wait_event(wait_q, _wait_for_wakeup(cqr));
	
	/* Request status is either done or failed. */
	rc = (cqr->status == DASD_CQR_FAILED) ? -EIO : 0;
	return rc;
}

/*
 * Attempts to start a special ccw queue and wait interruptible
 * for its completion.
 */
int
dasd_sleep_on_interruptible(dasd_ccw_req_t * cqr)
{
	wait_queue_head_t wait_q;
	dasd_device_t *device;
	int rc, finished;

	device = cqr->device;
	spin_lock_irq(get_ccwdev_lock(device->cdev));

	init_waitqueue_head (&wait_q);
	cqr->callback = dasd_wakeup_cb;
	cqr->callback_data = (void *) &wait_q;
	cqr->status = DASD_CQR_QUEUED;
	list_add_tail(&cqr->list, &device->ccw_queue);

	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);
	spin_unlock_irq(get_ccwdev_lock(device->cdev));

	finished = 0;
	while (!finished) {
		rc = wait_event_interruptible(wait_q, _wait_for_wakeup(cqr));
		if (rc != -ERESTARTSYS) {
			/* Request status is either done or failed. */
			rc = (cqr->status == DASD_CQR_FAILED) ? -EIO : 0;
			break;
		}
		spin_lock_irq(get_ccwdev_lock(device->cdev));
		if (cqr->status == DASD_CQR_IN_IO &&
		    device->discipline->term_IO(cqr) == 0) {
			list_del(&cqr->list);
			finished = 1;
		}
		spin_unlock_irq(get_ccwdev_lock(device->cdev));
	}
	return rc;
}

/*
 * Whoa nelly now it gets really hairy. For some functions (e.g. steal lock
 * for eckd devices) the currently running request has to be terminated
 * and be put back to status queued, before the special request is added
 * to the head of the queue. Then the special request is waited on normally.
 */
static inline int
_dasd_term_running_cqr(dasd_device_t *device)
{
	dasd_ccw_req_t *cqr;
	int rc;

	if (list_empty(&device->ccw_queue))
		return 0;
	cqr = list_entry(device->ccw_queue.next, dasd_ccw_req_t, list);
	rc = device->discipline->term_IO(cqr);
	if (rc == 0) {
		/* termination successful */
		cqr->status = DASD_CQR_QUEUED;
		cqr->startclk = cqr->stopclk = 0;
	}
	return rc;
}

int
dasd_sleep_on_immediatly(dasd_ccw_req_t * cqr)
{
	wait_queue_head_t wait_q;
	dasd_device_t *device;
	int rc;
	
	device = cqr->device;
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	rc = _dasd_term_running_cqr(device);
	if (rc) {
		spin_unlock_irq(get_ccwdev_lock(device->cdev));
		return rc;
	}
	
	init_waitqueue_head (&wait_q);
	cqr->callback = dasd_wakeup_cb;
	cqr->callback_data = (void *) &wait_q;
	cqr->status = DASD_CQR_QUEUED;
	list_add(&cqr->list, &device->ccw_queue);
	
	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);
	
	spin_unlock_irq(get_ccwdev_lock(device->cdev));

	wait_event(wait_q, _wait_for_wakeup(cqr));
	
	/* Request status is either done or failed. */
	rc = (cqr->status == DASD_CQR_FAILED) ? -EIO : 0;
	return rc;
}

/*
 * Cancels a request that was started with dasd_sleep_on_req.
 * This is usefull to timeout requests. The request will be
 * terminated if it is currently in i/o.
 * Returns 1 if the request has been terminated.
 */
int dasd_cancel_req(dasd_ccw_req_t *cqr)
{
	dasd_device_t *device = cqr->device;
	unsigned long flags;
	int rc;

	rc = 0;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	switch (cqr->status) {
	case DASD_CQR_QUEUED:
		/* request was not started - just set to failed */
		cqr->status = DASD_CQR_FAILED;
		break;
	case DASD_CQR_IN_IO:
		/* request in IO - terminate IO and release again */
		if (device->discipline->term_IO(cqr) != 0)
			/* what to do if unable to terminate ??????
			   e.g. not _IN_IO */
			cqr->status = DASD_CQR_FAILED;
		cqr->stopclk = get_clock();
		rc = 1;
		break;
	case DASD_CQR_DONE:
	case DASD_CQR_FAILED:
		/* already finished - do nothing */
		break;
	default:
		DEV_MESSAGE(KERN_ALERT, device,
			    "invalid status %02x in request",
			    cqr->status);
		BUG();

	}
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	dasd_schedule_bh(device);
	return rc;
}

/*
 * SECTION: Block device operations (request queue, partitions, open, release).
 */

/*
 * Dasd request queue function. Called from ll_rw_blk.c
 */
static void
do_dasd_request(request_queue_t * queue)
{
	dasd_device_t *device;

	device = (dasd_device_t *) queue->queuedata;
	spin_lock(get_ccwdev_lock(device->cdev));
	/* Get new request from the block device request queue */
	__dasd_process_blk_queue(device);
	/* Now check if the head of the ccw queue needs to be started. */
	__dasd_start_head(device);
	spin_unlock(get_ccwdev_lock(device->cdev));
}

/*
 * Allocate request queue and initialize gendisk info for device.
 */
static int
dasd_setup_blkdev(dasd_device_t * device)
{
	int max, rc;

	device->request_queue = kmalloc(sizeof (request_queue_t), GFP_KERNEL);
	if (device->request_queue == NULL)
		return -ENOMEM;
	device->request_queue->queuedata = device;
	rc = blk_init_queue(device->request_queue, do_dasd_request,
			    &device->request_queue_lock);
	if (rc)
		return rc;
	elevator_exit(device->request_queue);
	rc = elevator_init(device->request_queue, &elevator_noop);
	if (rc) {
		blk_cleanup_queue(device->request_queue);
		return rc;
	}
	blk_queue_hardsect_size(device->request_queue, device->bp_block);
	max = device->discipline->max_blocks << device->s2b_shift;
	blk_queue_max_sectors(device->request_queue, max);
	blk_queue_max_phys_segments(device->request_queue, -1L);
	blk_queue_max_hw_segments(device->request_queue, -1L);
	blk_queue_max_segment_size(device->request_queue, -1L);
	blk_queue_segment_boundary(device->request_queue, -1L);
	return 0;
}

/*
 * Deactivate and free request queue.
 */
static void
dasd_disable_blkdev(dasd_device_t * device)
{
	if (device->request_queue) {
		blk_cleanup_queue(device->request_queue);
		kfree(device->request_queue);
		device->request_queue = NULL;
	}
}

/*
 * Flush request on the request queue.
 */
static void
dasd_flush_request_queue(dasd_device_t * device)
{
	struct request *req;

	if (!device->request_queue)
		return;
	
	spin_lock_irq(&device->request_queue_lock);
	while (!list_empty(&device->request_queue->queue_head)) {
		req = elv_next_request(device->request_queue);
		if (req == NULL)
			break;
		dasd_end_request(req, 0);
		blkdev_dequeue_request(req);
	}
	spin_unlock_irq(&device->request_queue_lock);
}

static int
dasd_open(struct inode *inp, struct file *filp)
{
	dasd_device_t *device;
	int rc;
	
	if (dasd_probeonly) {
		MESSAGE(KERN_INFO,
			"No access to device (%d:%d) due to probeonly mode",
			major(inp->i_rdev), minor(inp->i_rdev));
		return -EPERM;
	}

	device = inp->i_bdev->bd_disk->private_data;
	if (device->state < DASD_STATE_BASIC) {
		DBF_DEV_EVENT(DBF_ERR, device, " %s",
			      " Cannot open unrecognized device");
		return -ENODEV;
	}
	rc = 0;

	if (atomic_inc_return(&device->open_count) == 1) {
		if (!try_module_get(device->discipline->owner)) {
			/* Discipline is currently unloaded! */
			atomic_dec(&device->open_count);
			rc = -ENODEV;
		}
	}
	return rc;
}

static int
dasd_release(struct inode *inp, struct file *filp)
{
	dasd_device_t *device;

	device = inp->i_bdev->bd_disk->private_data;

	if (device->state < DASD_STATE_ACCEPT) {
		DBF_DEV_EVENT(DBF_ERR, device, " %s",
			      " Cannot release unrecognized device");
		return -EINVAL;
	}
	if (atomic_dec_return(&device->open_count) == 0) {
		invalidate_buffers(inp->i_rdev);
		module_put(device->discipline->owner);
	}
	return 0;
}

struct
block_device_operations dasd_device_operations = {
	.owner		= THIS_MODULE,
	.open		= dasd_open,
	.release	= dasd_release,
	.ioctl		= dasd_ioctl,
};


static void
dasd_exit(void)
{
#ifdef CONFIG_PROC_FS
	dasd_proc_exit();
#endif
	dasd_ioctl_exit();
	dasd_gendisk_exit();
	dasd_devmap_exit();
	devfs_remove("dasd");
	if (dasd_debug_area != NULL) {
		debug_unregister(dasd_debug_area);
		dasd_debug_area = NULL;
	}
}

/*
 * SECTION: common functions for ccw_driver use
 */

/* initial attempt at a probe function. this can be simplified once
 * the other detection code is gone */
int
dasd_generic_probe (struct ccw_device *cdev, dasd_discipline_t *discipline)
{
	int devno;
	int ret = 0;

	snprintf(cdev->dev.name, DEVICE_NAME_SIZE,
		 "Direct Access Storage Device");

	devno = _ccw_device_get_device_number(cdev);
	if (dasd_autodetect
	    && (ret = dasd_add_range(devno, devno, DASD_FEATURE_DEFAULT))) {
		printk (KERN_WARNING
			"dasd_generic_probe: cannot autodetect %s\n",
			cdev->dev.bus_id);
		return ret;
	}

	if (!ret && (ret = dasd_add_sysfs_files(cdev))) {
		printk(KERN_WARNING
		       "dasd_generic_probe: could not add driverfs entries"
		       "for %s\n", cdev->dev.bus_id);
	}

	cdev->handler = &dasd_int_handler;

	if (dasd_autodetect ||
	    dasd_devmap_from_devno(devno) != 0) {
		/* => device was in dasd parameter line */
		ccw_device_set_online(cdev);
	}

	return ret;
}

/* this will one day be called from a global not_oper handler.
 * It is also used by driver_unregister during module unload */
int
dasd_generic_remove (struct ccw_device *cdev)
{
	struct dasd_device_t *device;

	device = cdev->dev.driver_data;
	cdev->dev.driver_data = NULL;
	if (device)
		kfree(device);
	return 0;
}

/* activate a device. This is called from dasd_{eckd,fba}_probe() when either
 * the device is detected for the first time and is supposed to be used
 * or the user has started activation through sysfs */
int
dasd_generic_set_online (struct ccw_device *cdev,
			 dasd_discipline_t *discipline)

{
	int devno;
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	int rc;

	if (cdev->dev.driver_data != NULL) /* already enabled */
		return 0;

	devno = _ccw_device_get_device_number(cdev);
	rc = dasd_add_range(devno, devno, DASD_FEATURE_DEFAULT);
	if (rc)
		return rc;

	if (!(devmap = dasd_devmap_from_devno (devno)))
		return 0; /* device is still disabled -> ignore it */

	if (IS_ERR(device = dasd_get_device(devmap))) {
		printk (KERN_WARNING "dasd_generic could not get %s\n",
				cdev->dev.bus_id);
		return PTR_ERR(device);
	}

	device->gdp->driverfs_dev = &cdev->dev;
	device->cdev = cdev;

	if (device->use_diag_flag)
		device->discipline = dasd_diag_discipline_pointer;

	rc = 0;
	if (!device->discipline ||
	    (rc = device->discipline->check_device(device))) {
		pr_debug("device %s is not diag (%d)\n", 
			 cdev->dev.bus_id, rc);
		if (device->private != NULL) {
			kfree(device->private);
			device->private = NULL;
		}
		device->discipline = discipline;
		rc = discipline->check_device(device);
	}

	if (rc) {
		printk (KERN_WARNING "dasd_generic found a bad device %s\n", 
			cdev->dev.bus_id);
		dasd_put_device(devmap);
		return rc;
	}

	dasd_set_target_state(device, DASD_STATE_ONLINE);
	if (device->state <= DASD_STATE_KNOWN) {
		printk (KERN_WARNING
			"dasd_generic discipline not found for %s\n",
			cdev->dev.bus_id);
		rc = -ENODEV;
		dasd_set_target_state(device, DASD_STATE_NEW);
	} else {
		pr_debug("dasd_generic device %s found\n",
				cdev->dev.bus_id);
		cdev->dev.driver_data = device;
	}

	dasd_put_device(devmap);
	/* FIXME: we have to wait for the root device but we don't want
	 * to wait for each single device but for all at once. */
	wait_event(dasd_init_waitq, _wait_for_device(device));
	return rc;
}

int
dasd_generic_set_offline (struct ccw_device *cdev)
{
	dasd_device_t *device;
	dasd_devmap_t *devmap;
	int devno;

	devno = _ccw_device_get_device_number(cdev);
	device = cdev->dev.driver_data;
	devmap = dasd_devmap_from_devno(devno);
	if (device == NULL || devmap == NULL)
		return -ENODEV;

	device = dasd_get_device(devmap);
	if (IS_ERR(device))
		return PTR_ERR(device);

	dasd_set_target_state(device, DASD_STATE_NEW);
	dasd_put_device(devmap);
	
	return 0;
}

/*
 * SECTION: files in sysfs
 */

/*
 * readonly controls the readonly status of a dasd
 */
static ssize_t
dasd_ro_show(struct device *dev, char *buf)
{
	dasd_device_t *device;

	device = dev->driver_data;
	if (!device)
		return snprintf(buf, PAGE_SIZE, "n/a\n");

	return snprintf(buf, PAGE_SIZE, device->ro_flag ? "1\n" : "0\n");
}

static ssize_t
dasd_ro_store(struct device *dev, const char *buf, size_t count)
{
	dasd_device_t *device = dev->driver_data;

	if (device)
		device->ro_flag = (buf[0] == '1') ? 1 : 0;
	return count;
}

static DEVICE_ATTR(readonly, 0644, dasd_ro_show, dasd_ro_store);

/*
 * use_diag controls whether the driver should use diag rather than ssch
 * to talk to the device
 */
/* TODO: Implement */
static ssize_t 
dasd_use_diag_show(struct device *dev, char *buf)
{
	dasd_device_t *device;

	device = dev->driver_data;
	if (!device)
		return sprintf(buf, "n/a\n");

	return sprintf(buf, device->use_diag_flag ? "1\n" : "0\n");
}

static ssize_t
dasd_use_diag_store(struct device *dev, const char *buf, size_t count)
{
	dasd_device_t *device = dev->driver_data;

	if (device)
		device->use_diag_flag = (buf[0] == '1') ? 1 : 0;
	return count;
}

static
DEVICE_ATTR(use_diag, 0644, dasd_use_diag_show, dasd_use_diag_store);

#if 0
/* this file shows the same information as /proc/dasd/devices using
 * an inaccaptable interface */
/* TODO: Split this up into smaller files! */
static ssize_t
dasd_devices_show(struct device *dev, char *buf)
{
	
	dasd_device_t *device;
	dasd_devmap_t *devmap;

	devmap = NULL;
	device = dev->driver_data;
	if (device)
		devmap = dasd_devmap_from_devno(device->devno);

	if (!devmap)
		return sprintf(buf, "unused\n");

	return min ((size_t) dasd_devices_print(devmap, buf), PAGE_SIZE);
}

static DEVICE_ATTR(dasd, 0444, dasd_devices_show, 0);
#endif

static ssize_t
dasd_discipline_show(struct device *dev, char *buf)
{
	dasd_device_t *device;

	device = dev->driver_data;
	if (!device || !device->discipline)
		return sprintf(buf, "none\n");
	return snprintf(buf, PAGE_SIZE, "%s\n", device->discipline->name);
}

static DEVICE_ATTR(discipline, 0444, dasd_discipline_show, NULL);

static int
dasd_add_sysfs_files(struct ccw_device *cdev)
{
	int ret;

	if (/* (ret = device_create_file(&cdev->dev, &dev_attr_dasd)) || */
	    (ret = device_create_file(&cdev->dev, &dev_attr_readonly)) ||
	    (ret = device_create_file(&cdev->dev, &dev_attr_discipline)) ||
	    (ret = device_create_file(&cdev->dev, &dev_attr_use_diag))) {
		device_remove_file(&cdev->dev, &dev_attr_discipline);
		device_remove_file(&cdev->dev, &dev_attr_readonly);
		/* device_remove_file(&cdev->dev, &dev_attr_dasd); */
	}
	return ret;
}

static int __init
dasd_init(void)
{
	int rc;

	init_waitqueue_head(&dasd_init_waitq);

	/* register 'common' DASD debug area, used faor all DBF_XXX calls */
	dasd_debug_area = debug_register("dasd", 0, 2, 8 * sizeof (long));
	if (dasd_debug_area == NULL) {
		rc = -ENOMEM;
		goto failed;
	}
	debug_register_view(dasd_debug_area, &debug_sprintf_view);
	debug_set_level(dasd_debug_area, DBF_ERR);

	DBF_EVENT(DBF_EMERG, "%s", "debug area created");

	if (devfs_mk_dir(NULL, "dasd", NULL)) {
		DBF_EVENT(DBF_ALERT, "%s", "no devfs");
		rc = -ENOSYS;
		goto failed;
	}
	rc = dasd_devmap_init();
	if (rc)
		goto failed;
	rc = dasd_gendisk_init();
	if (rc)
		goto failed;
	rc = dasd_parse();
	if (rc)
		goto failed;
	rc = dasd_ioctl_init();
	if (rc)
		goto failed;
#ifdef CONFIG_PROC_FS
	rc = dasd_proc_init();
	if (rc)
		goto failed;
#endif

	return 0;
failed:
	MESSAGE(KERN_INFO, "%s", "initialization not performed due to errors");
	dasd_exit();
	return rc;
}

module_init(dasd_init);
module_exit(dasd_exit);

EXPORT_SYMBOL(dasd_debug_area);

EXPORT_SYMBOL(dasd_add_request_head);
EXPORT_SYMBOL(dasd_add_request_tail);
EXPORT_SYMBOL(dasd_cancel_req);
EXPORT_SYMBOL(dasd_clear_timer);
EXPORT_SYMBOL(dasd_enable_device);
EXPORT_SYMBOL(dasd_int_handler);
EXPORT_SYMBOL(dasd_kfree_request);
EXPORT_SYMBOL(dasd_kick_device);
EXPORT_SYMBOL(dasd_kmalloc_request);
EXPORT_SYMBOL(dasd_schedule_bh);
EXPORT_SYMBOL(dasd_set_target_state);
EXPORT_SYMBOL(dasd_set_timer);
EXPORT_SYMBOL(dasd_sfree_request);
EXPORT_SYMBOL(dasd_sleep_on);
EXPORT_SYMBOL(dasd_sleep_on_immediatly);
EXPORT_SYMBOL(dasd_sleep_on_interruptible);
EXPORT_SYMBOL(dasd_smalloc_request);
EXPORT_SYMBOL(dasd_start_IO);
EXPORT_SYMBOL(dasd_term_IO);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: 1
 * tab-width: 8
 * End:
 */
