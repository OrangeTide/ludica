/* debug.h - runtime debug flags for lilpc */
#ifndef LILPC_DEBUG_H
#define LILPC_DEBUG_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Each flag is one bit in a uint64_t, mapped to an alphanumeric character:
 *   a-z = bits 0-25, A-Z = bits 26-51, 0-9 = bits 52-61
 * Pass characters via --debug=<chars> or LILPC_DEBUG=<chars> environment.
 */

/* assigned flags */
#define DBG_CPU		((uint64_t)1 << 0)	/* 'a' - CPU instruction trace */
#define DBG_FDC		((uint64_t)1 << 1)	/* 'b' - floppy disk controller */
#define DBG_DMA		((uint64_t)1 << 2)	/* 'c' - DMA controller */
#define DBG_PIC		((uint64_t)1 << 3)	/* 'd' - PIC interrupts */
#define DBG_PIT		((uint64_t)1 << 4)	/* 'e' - PIT timer */
#define DBG_EXIT	((uint64_t)1 << 5)	/* 'f' - dump state on exit */

/* convert character to bit position, -1 if invalid */
static inline int dbg_char_to_bit(char c)
{
	if (c >= 'a' && c <= 'z') return c - 'a';
	if (c >= 'A' && c <= 'Z') return c - 'A' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	return -1;
}

/* parse a string of flag characters into a bitmask */
static inline uint64_t dbg_parse(const char *s)
{
	uint64_t mask = 0;
	if (!s) return 0;
	for (; *s; s++) {
		int bit = dbg_char_to_bit(*s);
		if (bit >= 0)
			mask |= (uint64_t)1 << bit;
	}
	return mask;
}

#endif
