/*
 *	AX.25 release 037
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	AX.25 036	Jonathan(G4KLX)	Split from ax25_timer.c.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

static struct protocol_struct {
	struct protocol_struct *next;
	unsigned int pid;
	int (*func)(struct sk_buff *, ax25_cb *);
} *protocol_list;
static rwlock_t protocol_list_lock = RW_LOCK_UNLOCKED;

static struct linkfail_struct {
	struct linkfail_struct *next;
	void (*func)(ax25_cb *, int);
} *linkfail_list;
static spinlock_t linkfail_lock = SPIN_LOCK_UNLOCKED;

static struct listen_struct {
	struct listen_struct *next;
	ax25_address  callsign;
	struct net_device *dev;
} *listen_list;
static spinlock_t listen_lock = SPIN_LOCK_UNLOCKED;

int ax25_protocol_register(unsigned int pid, int (*func)(struct sk_buff *, ax25_cb *))
{
	struct protocol_struct *protocol;

	if (pid == AX25_P_TEXT || pid == AX25_P_SEGMENT)
		return 0;
#ifdef CONFIG_INET
	if (pid == AX25_P_IP || pid == AX25_P_ARP)
		return 0;
#endif
	if ((protocol = kmalloc(sizeof(*protocol), GFP_ATOMIC)) == NULL)
		return 0;

	protocol->pid  = pid;
	protocol->func = func;

	write_lock(&protocol_list_lock);
	protocol->next = protocol_list;
	protocol_list  = protocol;
	write_unlock(&protocol_list_lock);

	return 1;
}

void ax25_protocol_release(unsigned int pid)
{
	struct protocol_struct *s, *protocol;

	write_lock(&protocol_list_lock);
	protocol = protocol_list;
	if (protocol == NULL) {
		write_unlock(&protocol_list_lock);
		return;
	}

	if (protocol->pid == pid) {
		protocol_list = protocol->next;
		write_unlock(&protocol_list_lock);
		kfree(protocol);
		return;
	}

	while (protocol != NULL && protocol->next != NULL) {
		if (protocol->next->pid == pid) {
			s = protocol->next;
			protocol->next = protocol->next->next;
			write_unlock(&protocol_list_lock);
			kfree(s);
			return;
		}

		protocol = protocol->next;
	}
	write_unlock(&protocol_list_lock);
}

int ax25_linkfail_register(void (*func)(ax25_cb *, int))
{
	struct linkfail_struct *linkfail;
	unsigned long flags;

	if ((linkfail = kmalloc(sizeof(*linkfail), GFP_ATOMIC)) == NULL)
		return 0;

	linkfail->func = func;

	spin_lock_irqsave(&linkfail_lock, flags);
	linkfail->next = linkfail_list;
	linkfail_list  = linkfail;
	spin_unlock_irqrestore(&linkfail_lock, flags);

	return 1;
}

void ax25_linkfail_release(void (*func)(ax25_cb *, int))
{
	struct linkfail_struct *s, *linkfail;
	unsigned long flags;

	spin_lock_irqsave(&linkfail_lock, flags);
	linkfail = linkfail_list;
	if (linkfail == NULL)
		return;

	if (linkfail->func == func) {
		linkfail_list = linkfail->next;
		spin_unlock_irqrestore(&linkfail_lock, flags);
		kfree(linkfail);
		return;
	}

	while (linkfail != NULL && linkfail->next != NULL) {
		if (linkfail->next->func == func) {
			s = linkfail->next;
			linkfail->next = linkfail->next->next;
			spin_unlock_irqrestore(&linkfail_lock, flags);
			kfree(s);
			return;
		}

		linkfail = linkfail->next;
	}
	spin_unlock_irqrestore(&linkfail_lock, flags);
}

int ax25_listen_register(ax25_address *callsign, struct net_device *dev)
{
	struct listen_struct *listen;
	unsigned long flags;

	if (ax25_listen_mine(callsign, dev))
		return 0;

	if ((listen = kmalloc(sizeof(*listen), GFP_ATOMIC)) == NULL)
		return 0;

	listen->callsign = *callsign;
	listen->dev      = dev;

	spin_lock_irqsave(&listen_lock, flags);
	listen->next = listen_list;
	listen_list  = listen;
	spin_unlock_irqrestore(&listen_lock, flags);

	return 1;
}

void ax25_listen_release(ax25_address *callsign, struct net_device *dev)
{
	struct listen_struct *s, *listen;
	unsigned long flags;

	spin_lock_irqsave(&listen_lock, flags);
	listen = listen_list;
	if (listen == NULL)
		return;

	if (ax25cmp(&listen->callsign, callsign) == 0 && listen->dev == dev) {
		listen_list = listen->next;
		spin_unlock_irqrestore(&listen_lock, flags);
		kfree(listen);
		return;
	}

	while (listen != NULL && listen->next != NULL) {
		if (ax25cmp(&listen->next->callsign, callsign) == 0 && listen->next->dev == dev) {
			s = listen->next;
			listen->next = listen->next->next;
			spin_unlock_irqrestore(&listen_lock, flags);
			kfree(s);
			return;
		}

		listen = listen->next;
	}
	spin_unlock_irqrestore(&listen_lock, flags);
}

int (*ax25_protocol_function(unsigned int pid))(struct sk_buff *, ax25_cb *)
{
	int (*res)(struct sk_buff *, ax25_cb *) = NULL;
	struct protocol_struct *protocol;

	read_lock(&protocol_list_lock);
	for (protocol = protocol_list; protocol != NULL; protocol = protocol->next)
		if (protocol->pid == pid) {
			res = protocol->func;
			break;
		}
	read_unlock(&protocol_list_lock);

	return res;
}

int ax25_listen_mine(ax25_address *callsign, struct net_device *dev)
{
	struct listen_struct *listen;
	unsigned long flags;

	spin_lock_irqsave(&listen_lock, flags);
	for (listen = listen_list; listen != NULL; listen = listen->next)
		if (ax25cmp(&listen->callsign, callsign) == 0 && (listen->dev == dev || listen->dev == NULL))
			return 1;
	spin_unlock_irqrestore(&listen_lock, flags);

	return 0;
}

void ax25_link_failed(ax25_cb *ax25, int reason)
{
	struct linkfail_struct *linkfail;
	unsigned long flags;

	spin_lock_irqsave(&linkfail_lock, flags);
	for (linkfail = linkfail_list; linkfail != NULL; linkfail = linkfail->next)
		(linkfail->func)(ax25, reason);
	spin_unlock_irqrestore(&linkfail_lock, flags);
}

int ax25_protocol_is_registered(unsigned int pid)
{
	struct protocol_struct *protocol;
	int res = 0;

	read_lock(&protocol_list_lock);
	for (protocol = protocol_list; protocol != NULL; protocol = protocol->next)
		if (protocol->pid == pid) {
			res = 1;
			break;
		}
	read_unlock(&protocol_list_lock);

	return res;
}
