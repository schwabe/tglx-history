/* $Id: hscx_irq.c,v 1.16.6.2 2001/09/23 22:24:48 kai Exp $
 *
 * low level b-channel stuff for Siemens HSCX
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * This is an include file for fast inline IRQ stuff
 *
 */


static inline void
waitforCEC(struct IsdnCardState *cs, int hscx)
{
	int to = 50;

	while ((READHSCX(cs, hscx, HSCX_STAR) & 0x04) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforCEC timeout\n");
}


static inline void
waitforXFW(struct IsdnCardState *cs, int hscx)
{
	int to = 50;

	while ((!(READHSCX(cs, hscx, HSCX_STAR) & 0x44) == 0x40) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforXFW timeout\n");
}

static inline void
hscx_writeCMDR(struct IsdnCardState *cs, int hscx, u8 data)
{
	waitforCEC(cs, hscx);
	WRITEHSCX(cs, hscx, HSCX_CMDR, data);
}


static void
hscx_empty_fifo(struct BCState *bcs, int count)
{
	u8 *ptr;
	struct IsdnCardState *cs = bcs->cs;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hscx_empty_fifo");

	if (bcs->hw.hscx.rcvidx + count > HSCX_BUFMAX) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hscx_empty_fifo: incoming packet too large");
		hscx_writeCMDR(cs, bcs->hw.hscx.hscx, 0x80);
		bcs->hw.hscx.rcvidx = 0;
		return;
	}
	ptr = bcs->hw.hscx.rcvbuf + bcs->hw.hscx.rcvidx;
	bcs->hw.hscx.rcvidx += count;
	READHSCXFIFO(cs, bcs->hw.hscx.hscx, ptr, count);
	hscx_writeCMDR(cs, bcs->hw.hscx.hscx, 0x80);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "hscx_empty_fifo %c cnt %d",
			     bcs->hw.hscx.hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

static void
hscx_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int more, count;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	u8 *ptr;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hscx_fill_fifo");

	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	more = (bcs->mode == L1_MODE_TRANS) ? 1 : 0;
	if (bcs->tx_skb->len > fifo_size) {
		more = !0;
		count = fifo_size;
	} else
		count = bcs->tx_skb->len;

	waitforXFW(cs, bcs->hw.hscx.hscx);
	ptr = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt -= count;
	bcs->count += count;
	WRITEHSCXFIFO(cs, bcs->hw.hscx.hscx, ptr, count);
	hscx_writeCMDR(cs, bcs->hw.hscx.hscx, more ? 0x8 : 0xa);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "hscx_fill_fifo %c cnt %d",
			     bcs->hw.hscx.hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

static inline void
hscx_interrupt(struct IsdnCardState *cs, u8 val, u8 hscx)
{
	u8 r;
	struct BCState *bcs = cs->bcs + hscx;
	struct sk_buff *skb;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	int count;

	if (!test_bit(BC_FLG_INIT, &bcs->Flag))
		return;

	if (val & 0x80) {	/* RME */
		r = READHSCX(cs, hscx, HSCX_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!(r & 0x80)) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX invalid frame");
#ifdef ERROR_STATISTIC
				bcs->err_inv++;
#endif
			}
			if ((r & 0x40) && bcs->mode) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX RDO mode=%d",
						bcs->mode);
#ifdef ERROR_STATISTIC
				bcs->err_rdo++;
#endif
			}
			if (!(r & 0x20)) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX CRC error");
#ifdef ERROR_STATISTIC
				bcs->err_crc++;
#endif
			}
			hscx_writeCMDR(cs, hscx, 0x80);
		} else {
			count = READHSCX(cs, hscx, HSCX_RBCL) & (
				test_bit(HW_IPAC, &cs->HW_Flags)? 0x3f: 0x1f);
			if (count == 0)
				count = fifo_size;
			hscx_empty_fifo(bcs, count);
			if ((count = bcs->hw.hscx.rcvidx - 1) > 0) {
				if (cs->debug & L1_DEB_HSCX_FIFO)
					debugl1(cs, "HX Frame %d", count);
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "HSCX: receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), bcs->hw.hscx.rcvbuf, count);
					skb_queue_tail(&bcs->rqueue, skb);
				}
			}
		}
		bcs->hw.hscx.rcvidx = 0;
		sched_b_event(bcs, B_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		hscx_empty_fifo(bcs, fifo_size);
		if (bcs->mode == L1_MODE_TRANS) {
			/* receive audio data */
			if (!(skb = dev_alloc_skb(fifo_size)))
				printk(KERN_WARNING "HiSax: receive out of memory\n");
			else {
				memcpy(skb_put(skb, fifo_size), bcs->hw.hscx.rcvbuf, fifo_size);
				skb_queue_tail(&bcs->rqueue, skb);
			}
			bcs->hw.hscx.rcvidx = 0;
			sched_b_event(bcs, B_RCVBUFREADY);
		}
	}
	if (val & 0x10) {
		xmit_xpr_b(bcs);
	}
}

static void
reset_xmit(struct BCState *bcs)
{
	hscx_writeCMDR(bcs->cs, bcs->hw.hscx.hscx, 0x01);
}

static inline void
hscx_int_main(struct IsdnCardState *cs, u8 val)
{

	u8 exval;
	struct BCState *bcs;

	spin_lock(&cs->lock);
	if (val & 0x01) {
		bcs = cs->bcs + 1;
		exval = READHSCX(cs, 1, HSCX_EXIR);
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX B EXIR %x", exval);
		if (exval & 0x40) {
			xmit_xdu_b(bcs, reset_xmit);
		}
	}
	if (val & 0xf8) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX B interrupt %x", val);
		hscx_interrupt(cs, val, 1);
	}
	if (val & 0x02) {
		bcs = cs->bcs;
		exval = READHSCX(cs, 0, HSCX_EXIR);
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX A EXIR %x", exval);
		if (exval & 0x40) {
			xmit_xdu_b(bcs, reset_xmit);
		}
	}
	if (val & 0x04) {
		exval = READHSCX(cs, 0, HSCX_ISTA);
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX A interrupt %x", exval);
		hscx_interrupt(cs, exval, 0);
	}
	spin_unlock(&cs->lock);
}
