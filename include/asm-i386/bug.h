#ifndef _I386_BUG_H
#define _I386_BUG_H

#include <linux/config.h>

/*
 * Tell the user there is some problem. Beep too, so we can
 * see^H^H^Hhear bugs in early bootup as well!
 * The offending file and line are encoded after the "officially
 * undefined" opcode for parsing in the trap handler.
 */

#if 1	/* Set to zero for a slightly smaller kernel */
#define BUG()				\
 __asm__ __volatile__(	"ud2\n"		\
			"\t.word %c0\n"	\
			"\t.long %c1\n"	\
			 : : "i" (__LINE__), "i" (__FILE__))
#else
#define BUG() __asm__ __volatile__("ud2\n")
#endif

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

#endif
