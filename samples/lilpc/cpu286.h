/* cpu286.h - Intel 80286 CPU emulation for lilpc */
#ifndef CPU286_H
#define CPU286_H

#include <stdint.h>
#include <stdbool.h>

struct lilpc;

/* flag bits */
#define FLAG_CF		0x0001
#define FLAG_PF		0x0004
#define FLAG_AF		0x0010
#define FLAG_ZF		0x0040
#define FLAG_SF		0x0080
#define FLAG_TF		0x0100
#define FLAG_IF		0x0200
#define FLAG_DF		0x0400
#define FLAG_OF		0x0800
#define FLAG_IOPL_MASK	0x3000
#define FLAG_IOPL_SHIFT	12
#define FLAG_NT		0x4000
#define FLAGS_ALWAYS_ON	0x0002	/* bit 1 is always 1 on 286 */

/* segment register indices */
#define SEG_ES	0
#define SEG_CS	1
#define SEG_SS	2
#define SEG_DS	3
#define SEG_NONE -1

/* segment descriptor cache (hidden part of segment register) */
typedef struct {
	uint16_t sel;		/* visible selector value */
	uint32_t base;		/* cached base address (24-bit on 286) */
	uint16_t limit;		/* segment limit */
	uint8_t access;		/* access rights byte */
} seg_reg_t;

/* descriptor table register (GDTR/IDTR) */
typedef struct {
	uint32_t base;		/* 24-bit base */
	uint16_t limit;		/* table limit */
} dt_reg_t;

typedef struct cpu286 {
	/* general purpose registers - little-endian unions */
	union { struct { uint8_t al, ah; }; uint16_t ax; };
	union { struct { uint8_t cl, ch; }; uint16_t cx; };
	union { struct { uint8_t dl, dh; }; uint16_t dx; };
	union { struct { uint8_t bl, bh; }; uint16_t bx; };
	uint16_t sp, bp, si, di;
	uint16_t ip;
	uint16_t flags;

	/* segment registers with descriptor caches */
	seg_reg_t seg[4];	/* ES, CS, SS, DS */

	/* system registers (286 protected mode) */
	uint16_t msw;		/* machine status word (CR0 low 16) */
	dt_reg_t gdtr;
	dt_reg_t idtr;
	seg_reg_t ldtr;		/* local descriptor table register */
	seg_reg_t tr;		/* task register */

	/* CPU state */
	bool halted;
	bool irq_pending;	/* set by PIC when interrupt ready */
	uint64_t cycles;	/* total cycles executed */
	int trap_after;		/* raise INT 1 after next instruction */

	/* prefix state (reset each instruction) */
	int seg_override;	/* SEG_NONE or SEG_ES/CS/SS/DS */
	int rep_mode;		/* 0=none, 1=REP/REPZ, 2=REPNZ */

	/* debug: previous instruction location */
	uint16_t prev_cs, prev_ip;
} cpu286_t;

/* MSW bits */
#define MSW_PE	0x0001	/* protection enable */
#define MSW_MP	0x0002	/* monitor processor extension */
#define MSW_EM	0x0004	/* emulate processor extension */
#define MSW_TS	0x0008	/* task switched */

void cpu286_init(cpu286_t *cpu);
void cpu286_reset(cpu286_t *cpu);
int cpu286_step(struct lilpc *pc);	/* returns cycles consumed */
void cpu286_interrupt(struct lilpc *pc, int num);

#endif
