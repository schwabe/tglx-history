/*
 * linux/arch/sparc/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

static unsigned long
search_one_table(const struct exception_table_entry *start,
		 const struct exception_table_entry *end,
		 unsigned long value, unsigned long *g2)
{
	const struct exception_table_entry *walk;

	/* Single insn entries are encoded as:
	 *	word 1:	insn address
	 *	word 2:	fixup code address
	 *
	 * Range entries are encoded as:
	 *	word 1: first insn address
	 *	word 2: 0
	 *	word 3: last insn address + 4 bytes
	 *	word 4: fixup code address
	 *
	 * See asm/uaccess.h for more details.
	 */

	/* 1. Try to find an exact match. */
	for (walk = start; walk <= end; walk++) {
		if (walk->fixup == 0) {
			/* A range entry, skip both parts. */
			walk++;
			continue;
		}

		if (walk->insn == value)
			return walk->fixup;
	}

	/* 2. Try to find a range match. */
	for (walk = start; walk <= (end - 1); walk++) {
		if (walk->fixup)
			continue;

		if (walk[0].insn <= value &&
		    walk[1].insn > value) {
			*g2 = (value - walk[0].insn) / 4;
			return walk[1].fixup;
		}
		walk++;
	}

        return 0;
}

extern spinlock_t modlist_lock;

unsigned long
search_exception_table(unsigned long addr, unsigned long *g2)
{
	unsigned long ret = 0;

#ifndef CONFIG_MODULES
	/* There is only the kernel to search.  */
	ret = search_one_table(__start___ex_table,
			       __stop___ex_table-1, addr, g2);
	return ret;
#else
	unsigned long flags;
	struct list_head *i;

	/* The kernel is the last "module" -- no need to treat it special.  */
	spin_lock_irqsave(&modlist_lock, flags);
	list_for_each(i, &extables) {
		struct exception_table *ex =
			list_entry(i, struct exception_table, list);
		if (ex->num_entries == 0)
			continue;
		ret = search_one_table(ex->entry,
				       ex->entry + ex->num_entries - 1,
				       addr, g2);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&modlist_lock, flags);
	return ret;
#endif
}
