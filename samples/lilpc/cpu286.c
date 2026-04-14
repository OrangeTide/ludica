/* cpu286.c - Intel 80286 CPU emulation
 *
 * Interpretive decode-dispatch with per-instruction cycle counting.
 * Cycle counts from Intel 286 Programmer's Reference Manual.
 *
 * At 6 MHz, one CGA scanline ≈ 382 CPU cycles. I/O access triggers
 * device catch-up to cpu.cycles for sub-scanline timing accuracy.
 */
#include "cpu286.h"
#include "lilpc.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Parity lookup table: 1 = even parity (PF set), 0 = odd
 * ================================================================ */
static const uint8_t parity_table[256] = {
	1,0,0,1,0,1,1,0, 0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1, 1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1, 1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0, 0,1,1,0,1,0,0,1,
	0,1,1,0,1,0,0,1, 1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0, 0,1,1,0,1,0,0,1,
	1,0,0,1,0,1,1,0, 0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1, 1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1, 1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0, 0,1,1,0,1,0,0,1,
	1,0,0,1,0,1,1,0, 0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1, 1,0,0,1,0,1,1,0,
	1,0,0,1,0,1,1,0, 0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1, 1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1, 1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0, 0,1,1,0,1,0,0,1,
};

/* ================================================================
 * Instruction fetch
 * ================================================================ */
static inline uint8_t fetch_u8(lilpc_t *pc)
{
	cpu286_t *cpu = &pc->cpu;
	uint8_t val = bus_read8(&pc->bus,
		(uint32_t)cpu->seg[SEG_CS].base + cpu->ip);
	cpu->ip++;
	return val;
}

static inline uint16_t fetch_u16(lilpc_t *pc)
{
	uint8_t lo = fetch_u8(pc);
	uint8_t hi = fetch_u8(pc);
	return lo | ((uint16_t)hi << 8);
}

/* ================================================================
 * Register access by ModR/M index
 * ================================================================ */
static inline uint8_t get_reg8(cpu286_t *cpu, int idx)
{
	/* 0=AL 1=CL 2=DL 3=BL 4=AH 5=CH 6=DH 7=BH */
	switch (idx & 7) {
	case 0: return cpu->al;  case 1: return cpu->cl;
	case 2: return cpu->dl;  case 3: return cpu->bl;
	case 4: return cpu->ah;  case 5: return cpu->ch;
	case 6: return cpu->dh;  case 7: return cpu->bh;
	}
	return 0;
}

static inline void set_reg8(cpu286_t *cpu, int idx, uint8_t val)
{
	switch (idx & 7) {
	case 0: cpu->al = val; break;  case 1: cpu->cl = val; break;
	case 2: cpu->dl = val; break;  case 3: cpu->bl = val; break;
	case 4: cpu->ah = val; break;  case 5: cpu->ch = val; break;
	case 6: cpu->dh = val; break;  case 7: cpu->bh = val; break;
	}
}

static inline uint16_t get_reg16(cpu286_t *cpu, int idx)
{
	/* 0=AX 1=CX 2=DX 3=BX 4=SP 5=BP 6=SI 7=DI */
	switch (idx & 7) {
	case 0: return cpu->ax;  case 1: return cpu->cx;
	case 2: return cpu->dx;  case 3: return cpu->bx;
	case 4: return cpu->sp;  case 5: return cpu->bp;
	case 6: return cpu->si;  case 7: return cpu->di;
	}
	return 0;
}

static inline void set_reg16(cpu286_t *cpu, int idx, uint16_t val)
{
	switch (idx & 7) {
	case 0: cpu->ax = val; break;  case 1: cpu->cx = val; break;
	case 2: cpu->dx = val; break;  case 3: cpu->bx = val; break;
	case 4: cpu->sp = val; break;  case 5: cpu->bp = val; break;
	case 6: cpu->si = val; break;  case 7: cpu->di = val; break;
	}
}

/* ================================================================
 * Segment/memory helpers
 * ================================================================ */
static inline int eff_seg(cpu286_t *cpu, int def_seg)
{
	return (cpu->seg_override != SEG_NONE) ? cpu->seg_override : def_seg;
}

static inline uint8_t read_mem8(lilpc_t *pc, int seg, uint16_t off)
{
	return bus_read8(&pc->bus, pc->cpu.seg[seg].base + off);
}

static inline uint16_t read_mem16(lilpc_t *pc, int seg, uint16_t off)
{
	return bus_read16(&pc->bus, pc->cpu.seg[seg].base + off);
}

static inline void write_mem8(lilpc_t *pc, int seg, uint16_t off, uint8_t v)
{
	bus_write8(&pc->bus, pc->cpu.seg[seg].base + off, v);
}

static inline void write_mem16(lilpc_t *pc, int seg, uint16_t off, uint16_t v)
{
	bus_write16(&pc->bus, pc->cpu.seg[seg].base + off, v);
}

/* ================================================================
 * ModR/M decoder (16-bit addressing only, no SIB on 286)
 * ================================================================ */
typedef struct {
	int mod, reg, rm;
	uint16_t ea;	/* effective address offset */
	int seg;	/* segment for memory access */
	bool is_mem;	/* true if memory operand (mod != 3) */
} modrm_t;

static modrm_t decode_modrm(lilpc_t *pc)
{
	cpu286_t *cpu = &pc->cpu;
	uint8_t byte = fetch_u8(pc);
	modrm_t m;
	m.mod = (byte >> 6) & 3;
	m.reg = (byte >> 3) & 7;
	m.rm = byte & 7;
	m.is_mem = (m.mod != 3);

	if (!m.is_mem) {
		m.seg = SEG_DS;
		m.ea = 0;
		return m;
	}

	int def_seg = SEG_DS;
	uint16_t ea = 0;

	switch (m.rm) {
	case 0: ea = cpu->bx + cpu->si; break;
	case 1: ea = cpu->bx + cpu->di; break;
	case 2: ea = cpu->bp + cpu->si; def_seg = SEG_SS; break;
	case 3: ea = cpu->bp + cpu->di; def_seg = SEG_SS; break;
	case 4: ea = cpu->si; break;
	case 5: ea = cpu->di; break;
	case 6:
		if (m.mod == 0) {
			ea = fetch_u16(pc);
		} else {
			ea = cpu->bp;
			def_seg = SEG_SS;
		}
		break;
	case 7: ea = cpu->bx; break;
	}

	if (m.mod == 1)
		ea += (int16_t)(int8_t)fetch_u8(pc);
	else if (m.mod == 2)
		ea += fetch_u16(pc);

	m.ea = ea;
	m.seg = eff_seg(cpu, def_seg);
	return m;
}

/* Read/write through decoded ModR/M */
static inline uint8_t read_rm8(lilpc_t *pc, modrm_t *m)
{
	if (!m->is_mem) return get_reg8(&pc->cpu, m->rm);
	return read_mem8(pc, m->seg, m->ea);
}

static inline uint16_t read_rm16(lilpc_t *pc, modrm_t *m)
{
	if (!m->is_mem) return get_reg16(&pc->cpu, m->rm);
	return read_mem16(pc, m->seg, m->ea);
}

static inline void write_rm8(lilpc_t *pc, modrm_t *m, uint8_t val)
{
	if (!m->is_mem) set_reg8(&pc->cpu, m->rm, val);
	else write_mem8(pc, m->seg, m->ea, val);
}

static inline void write_rm16(lilpc_t *pc, modrm_t *m, uint16_t val)
{
	if (!m->is_mem) set_reg16(&pc->cpu, m->rm, val);
	else write_mem16(pc, m->seg, m->ea, val);
}

/* ================================================================
 * Flag computation
 * ================================================================ */
static inline void set_szp8(cpu286_t *cpu, uint8_t r)
{
	cpu->flags &= ~(FLAG_SF | FLAG_ZF | FLAG_PF);
	if (r & 0x80) cpu->flags |= FLAG_SF;
	if (r == 0) cpu->flags |= FLAG_ZF;
	if (parity_table[r]) cpu->flags |= FLAG_PF;
}

static inline void set_szp16(cpu286_t *cpu, uint16_t r)
{
	cpu->flags &= ~(FLAG_SF | FLAG_ZF | FLAG_PF);
	if (r & 0x8000) cpu->flags |= FLAG_SF;
	if (r == 0) cpu->flags |= FLAG_ZF;
	if (parity_table[r & 0xFF]) cpu->flags |= FLAG_PF;
}

static inline void flags_add8(cpu286_t *cpu, uint8_t a, uint8_t b, uint16_t r)
{
	set_szp8(cpu, (uint8_t)r);
	cpu->flags &= ~(FLAG_CF | FLAG_OF | FLAG_AF);
	if (r & 0x100) cpu->flags |= FLAG_CF;
	if ((a ^ b ^ r) & 0x10) cpu->flags |= FLAG_AF;
	if (((a ^ r) & (b ^ r)) & 0x80) cpu->flags |= FLAG_OF;
}

static inline void flags_add16(cpu286_t *cpu, uint16_t a, uint16_t b, uint32_t r)
{
	set_szp16(cpu, (uint16_t)r);
	cpu->flags &= ~(FLAG_CF | FLAG_OF | FLAG_AF);
	if (r & 0x10000) cpu->flags |= FLAG_CF;
	if ((a ^ b ^ r) & 0x10) cpu->flags |= FLAG_AF;
	if (((a ^ r) & (b ^ r)) & 0x8000) cpu->flags |= FLAG_OF;
}

static inline void flags_sub8(cpu286_t *cpu, uint8_t a, uint8_t b, uint16_t r)
{
	set_szp8(cpu, (uint8_t)r);
	cpu->flags &= ~(FLAG_CF | FLAG_OF | FLAG_AF);
	if (a < b) cpu->flags |= FLAG_CF;
	if ((a ^ b ^ r) & 0x10) cpu->flags |= FLAG_AF;
	if (((a ^ b) & (a ^ r)) & 0x80) cpu->flags |= FLAG_OF;
}

static inline void flags_sub16(cpu286_t *cpu, uint16_t a, uint16_t b, uint32_t r)
{
	set_szp16(cpu, (uint16_t)r);
	cpu->flags &= ~(FLAG_CF | FLAG_OF | FLAG_AF);
	if (a < b) cpu->flags |= FLAG_CF;
	if ((a ^ b ^ r) & 0x10) cpu->flags |= FLAG_AF;
	if (((a ^ b) & (a ^ r)) & 0x8000) cpu->flags |= FLAG_OF;
}

static inline void flags_logic8(cpu286_t *cpu, uint8_t r)
{
	set_szp8(cpu, r);
	cpu->flags &= ~(FLAG_CF | FLAG_OF | FLAG_AF);
}

static inline void flags_logic16(cpu286_t *cpu, uint16_t r)
{
	set_szp16(cpu, r);
	cpu->flags &= ~(FLAG_CF | FLAG_OF | FLAG_AF);
}

/* ================================================================
 * ALU operations (shared by opcodes 00-3D and groups 80-83)
 *
 * 0=ADD 1=OR 2=ADC 3=SBB 4=AND 5=SUB 6=XOR 7=CMP
 * Returns result. For CMP, result is discarded by caller.
 * ================================================================ */
static uint8_t alu8(cpu286_t *cpu, int op, uint8_t a, uint8_t b)
{
	uint16_t r;
	uint16_t cf = (cpu->flags & FLAG_CF) ? 1 : 0;

	switch (op) {
	case 0: /* ADD */ r = (uint16_t)a + b; flags_add8(cpu, a, b, r); break;
	case 1: /* OR  */ r = a | b; flags_logic8(cpu, (uint8_t)r); break;
	case 2: /* ADC */ r = (uint16_t)a + b + cf; flags_add8(cpu, a, b, r); break;
	case 3: /* SBB */ r = (uint16_t)a - b - cf; flags_sub8(cpu, a, b + cf, r);
		/* fix CF for borrow chain */
		cpu->flags &= ~FLAG_CF;
		if ((uint16_t)a < (uint16_t)b + cf) cpu->flags |= FLAG_CF;
		break;
	case 4: /* AND */ r = a & b; flags_logic8(cpu, (uint8_t)r); break;
	case 5: /* SUB */ r = (uint16_t)a - b; flags_sub8(cpu, a, b, r); break;
	case 6: /* XOR */ r = a ^ b; flags_logic8(cpu, (uint8_t)r); break;
	case 7: /* CMP */ r = (uint16_t)a - b; flags_sub8(cpu, a, b, r); break;
	default: r = 0; break;
	}
	return (uint8_t)r;
}

static uint16_t alu16(cpu286_t *cpu, int op, uint16_t a, uint16_t b)
{
	uint32_t r;
	uint32_t cf = (cpu->flags & FLAG_CF) ? 1 : 0;

	switch (op) {
	case 0: r = (uint32_t)a + b; flags_add16(cpu, a, b, r); break;
	case 1: r = a | b; flags_logic16(cpu, (uint16_t)r); break;
	case 2: r = (uint32_t)a + b + cf; flags_add16(cpu, a, b, r); break;
	case 3: r = (uint32_t)a - b - cf; flags_sub16(cpu, a, b + cf, r);
		cpu->flags &= ~FLAG_CF;
		if ((uint32_t)a < (uint32_t)b + cf) cpu->flags |= FLAG_CF;
		break;
	case 4: r = a & b; flags_logic16(cpu, (uint16_t)r); break;
	case 5: r = (uint32_t)a - b; flags_sub16(cpu, a, b, r); break;
	case 6: r = a ^ b; flags_logic16(cpu, (uint16_t)r); break;
	case 7: r = (uint32_t)a - b; flags_sub16(cpu, a, b, r); break;
	default: r = 0; break;
	}
	return (uint16_t)r;
}

/* ================================================================
 * Stack operations
 * ================================================================ */
static inline void push16(lilpc_t *pc, uint16_t val)
{
	pc->cpu.sp -= 2;
	write_mem16(pc, SEG_SS, pc->cpu.sp, val);
}

static inline uint16_t pop16(lilpc_t *pc)
{
	uint16_t val = read_mem16(pc, SEG_SS, pc->cpu.sp);
	pc->cpu.sp += 2;
	return val;
}

/* ================================================================
 * Shift/rotate operations (group 2)
 * ================================================================ */
static uint8_t shift8(cpu286_t *cpu, int op, uint8_t val, int count)
{
	uint16_t r;
	if (count == 0) return val;
	count &= 0x1F; /* 286 masks to 5 bits */
	if (count == 0) return val;

	switch (op) {
	case 0: /* ROL */
		for (int i = 0; i < count; i++) {
			int msb = (val >> 7) & 1;
			val = (val << 1) | msb;
		}
		cpu->flags = (cpu->flags & ~FLAG_CF) | (val & 1);
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 7) ^ val) & 1)
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 1: /* ROR */
		for (int i = 0; i < count; i++) {
			int lsb = val & 1;
			val = (val >> 1) | (lsb << 7);
		}
		cpu->flags = (cpu->flags & ~FLAG_CF) | ((val >> 7) & 1);
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 7) ^ (val >> 6)) & 1)
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 2: /* RCL */
		for (int i = 0; i < count; i++) {
			int cf = (cpu->flags & FLAG_CF) ? 1 : 0;
			cpu->flags &= ~FLAG_CF;
			if (val & 0x80) cpu->flags |= FLAG_CF;
			val = (val << 1) | cf;
		}
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 7) & 1) ^ ((cpu->flags & FLAG_CF) ? 1 : 0))
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 3: /* RCR */
		for (int i = 0; i < count; i++) {
			int cf = (cpu->flags & FLAG_CF) ? 1 : 0;
			cpu->flags &= ~FLAG_CF;
			if (val & 1) cpu->flags |= FLAG_CF;
			val = (val >> 1) | (cf << 7);
		}
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 7) ^ (val >> 6)) & 1)
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 4: /* SHL/SAL */
	case 6: /* SHL/SAL (undocumented alias) */
		r = (uint16_t)val << count;
		cpu->flags &= ~FLAG_CF;
		if ((val << (count - 1)) & 0x80) cpu->flags |= FLAG_CF;
		val = (uint8_t)r;
		set_szp8(cpu, val);
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 7) & 1) ^ ((cpu->flags & FLAG_CF) ? 1 : 0))
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 5: /* SHR */
		cpu->flags &= ~FLAG_CF;
		if ((val >> (count - 1)) & 1) cpu->flags |= FLAG_CF;
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (val & 0x80) cpu->flags |= FLAG_OF;
		}
		val >>= count;
		set_szp8(cpu, val);
		return val;
	case 7: /* SAR */
		cpu->flags &= ~FLAG_CF;
		if (((int8_t)val >> (count - 1)) & 1) cpu->flags |= FLAG_CF;
		val = (uint8_t)((int8_t)val >> count);
		set_szp8(cpu, val);
		if (count == 1)
			cpu->flags &= ~FLAG_OF; /* OF always 0 for SAR with count=1 */
		return val;
	}
	return val;
}

static uint16_t shift16(cpu286_t *cpu, int op, uint16_t val, int count)
{
	uint32_t r;
	if (count == 0) return val;
	count &= 0x1F;
	if (count == 0) return val;

	switch (op) {
	case 0: /* ROL */
		for (int i = 0; i < count; i++) {
			int msb = (val >> 15) & 1;
			val = (val << 1) | msb;
		}
		cpu->flags = (cpu->flags & ~FLAG_CF) | (val & 1);
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 15) ^ val) & 1)
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 1: /* ROR */
		for (int i = 0; i < count; i++) {
			int lsb = val & 1;
			val = (val >> 1) | (lsb << 15);
		}
		cpu->flags = (cpu->flags & ~FLAG_CF) | ((val >> 15) & 1);
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 15) ^ (val >> 14)) & 1)
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 2: /* RCL */
		for (int i = 0; i < count; i++) {
			int cf = (cpu->flags & FLAG_CF) ? 1 : 0;
			cpu->flags &= ~FLAG_CF;
			if (val & 0x8000) cpu->flags |= FLAG_CF;
			val = (val << 1) | cf;
		}
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 15) & 1) ^ ((cpu->flags & FLAG_CF) ? 1 : 0))
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 3: /* RCR */
		for (int i = 0; i < count; i++) {
			int cf = (cpu->flags & FLAG_CF) ? 1 : 0;
			cpu->flags &= ~FLAG_CF;
			if (val & 1) cpu->flags |= FLAG_CF;
			val = (val >> 1) | (cf << 15);
		}
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 15) ^ (val >> 14)) & 1)
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 4: /* SHL */
	case 6:
		r = (uint32_t)val << count;
		cpu->flags &= ~FLAG_CF;
		if ((val << (count - 1)) & 0x8000) cpu->flags |= FLAG_CF;
		val = (uint16_t)r;
		set_szp16(cpu, val);
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (((val >> 15) & 1) ^ ((cpu->flags & FLAG_CF) ? 1 : 0))
				cpu->flags |= FLAG_OF;
		}
		return val;
	case 5: /* SHR */
		cpu->flags &= ~FLAG_CF;
		if ((val >> (count - 1)) & 1) cpu->flags |= FLAG_CF;
		if (count == 1) {
			cpu->flags &= ~FLAG_OF;
			if (val & 0x8000) cpu->flags |= FLAG_OF;
		}
		val >>= count;
		set_szp16(cpu, val);
		return val;
	case 7: /* SAR */
		cpu->flags &= ~FLAG_CF;
		if (((int16_t)val >> (count - 1)) & 1) cpu->flags |= FLAG_CF;
		val = (uint16_t)((int16_t)val >> count);
		set_szp16(cpu, val);
		if (count == 1)
			cpu->flags &= ~FLAG_OF;
		return val;
	}
	return val;
}

/* ================================================================
 * Condition code evaluation (for Jcc, SETcc, CMOVcc)
 * ================================================================ */
static inline bool eval_cc(cpu286_t *cpu, int cc)
{
	switch (cc) {
	case 0x0: return (cpu->flags & FLAG_OF) != 0;		/* O */
	case 0x1: return (cpu->flags & FLAG_OF) == 0;		/* NO */
	case 0x2: return (cpu->flags & FLAG_CF) != 0;		/* B/C/NAE */
	case 0x3: return (cpu->flags & FLAG_CF) == 0;		/* AE/NB/NC */
	case 0x4: return (cpu->flags & FLAG_ZF) != 0;		/* E/Z */
	case 0x5: return (cpu->flags & FLAG_ZF) == 0;		/* NE/NZ */
	case 0x6: return (cpu->flags & (FLAG_CF|FLAG_ZF)) != 0; /* BE/NA */
	case 0x7: return (cpu->flags & (FLAG_CF|FLAG_ZF)) == 0; /* A/NBE */
	case 0x8: return (cpu->flags & FLAG_SF) != 0;		/* S */
	case 0x9: return (cpu->flags & FLAG_SF) == 0;		/* NS */
	case 0xA: return (cpu->flags & FLAG_PF) != 0;		/* P/PE */
	case 0xB: return (cpu->flags & FLAG_PF) == 0;		/* NP/PO */
	case 0xC: /* L/NGE: SF != OF */
		return !!(cpu->flags & FLAG_SF) != !!(cpu->flags & FLAG_OF);
	case 0xD: /* GE/NL: SF == OF */
		return !!(cpu->flags & FLAG_SF) == !!(cpu->flags & FLAG_OF);
	case 0xE: /* LE/NG: ZF=1 or SF != OF */
		return (cpu->flags & FLAG_ZF) ||
			(!!(cpu->flags & FLAG_SF) != !!(cpu->flags & FLAG_OF));
	case 0xF: /* G/NLE: ZF=0 and SF == OF */
		return !(cpu->flags & FLAG_ZF) &&
			(!!(cpu->flags & FLAG_SF) == !!(cpu->flags & FLAG_OF));
	}
	return false;
}

/* ================================================================
 * Interrupt dispatch
 * ================================================================ */
void cpu286_interrupt(lilpc_t *pc, int num)
{
	cpu286_t *cpu = &pc->cpu;

	if (cpu->msw & MSW_PE) {
		/* TODO: protected mode interrupt through IDT */
		fprintf(stderr, "lilpc: PM interrupt %02Xh not implemented\n", num);
		cpu->halted = true;
		return;
	}

	/* Real mode: read vector from IVT at 0000:(num*4) */
	uint16_t off = bus_read16(&pc->bus, num * 4);
	uint16_t seg = bus_read16(&pc->bus, num * 4 + 2);

	if (num == 0) {
		static int div_trace = 0;
		if (div_trace < 3) {
			uint32_t addr = cpu->seg[SEG_CS].base + cpu->ip;
			fprintf(stderr, "TRACE: DIV exception at %04X:%04X AX=%04X DX=%04X CX=%04X BX=%04X bytes:",
				cpu->seg[SEG_CS].sel, cpu->ip,
				cpu->ax, cpu->dx, cpu->cx, cpu->bx);
			for (int i = -4; i < 8; i++)
				fprintf(stderr, " %02X", bus_read8(&pc->bus, addr + i));
			fprintf(stderr, "\n");
		}
		div_trace++;
	}

	push16(pc, cpu->flags | FLAGS_ALWAYS_ON);
	push16(pc, cpu->seg[SEG_CS].sel);
	push16(pc, cpu->ip);

	cpu->flags &= ~(FLAG_IF | FLAG_TF);
	cpu->ip = off;
	cpu->seg[SEG_CS].sel = seg;
	cpu->seg[SEG_CS].base = (uint32_t)seg << 4;
	cpu->halted = false;
}

/* ================================================================
 * CPU init / reset
 * ================================================================ */
void cpu286_init(cpu286_t *cpu)
{
	memset(cpu, 0, sizeof(*cpu));
	cpu286_reset(cpu);
}

void cpu286_reset(cpu286_t *cpu)
{
	cpu->ax = cpu->bx = cpu->cx = cpu->dx = 0;
	cpu->sp = cpu->bp = cpu->si = cpu->di = 0;
	cpu->flags = FLAGS_ALWAYS_ON;
	cpu->msw = 0xFFF0; /* 286 reset value */
	cpu->ip = 0x0000;

	/* CS = F000h, base = FF0000h on 286 (but wraps to F0000h with A20 off) */
	/* For XT with 20-bit bus, CS:IP = FFFF:0000 = physical FFFF0h */
	cpu->seg[SEG_CS].sel = 0xFFFF;
	cpu->seg[SEG_CS].base = 0xFFFF0; /* real mode: sel << 4 */

	/* Wait, 286 reset sets CS base to FF0000h (24-bit).
	 * But with A20 disabled, physical FFFF0 maps to the ROM.
	 * The first instruction at FFFF:0000 is typically JMP F000:xxxx.
	 * Let's use the standard real-mode calculation. */
	cpu->seg[SEG_CS].base = 0xFFFF << 4; /* = 0xFFFF0 */
	cpu->seg[SEG_CS].limit = 0xFFFF;
	cpu->seg[SEG_CS].access = 0x93;

	cpu->seg[SEG_DS].sel = 0;
	cpu->seg[SEG_DS].base = 0;
	cpu->seg[SEG_DS].limit = 0xFFFF;
	cpu->seg[SEG_ES] = cpu->seg[SEG_DS];
	cpu->seg[SEG_SS] = cpu->seg[SEG_DS];

	cpu->idtr.base = 0;
	cpu->idtr.limit = 0x3FF; /* real mode IVT: 256 entries × 4 bytes */

	cpu->halted = false;
	cpu->irq_pending = false;
	cpu->seg_override = SEG_NONE;
	cpu->rep_mode = 0;
	cpu->trap_after = 0;
}

/* ================================================================
 * LOADALL (undocumented 286: opcode 0F 05)
 * Loads entire CPU state from memory at physical address 800h-866h
 * ================================================================ */
static int do_loadall(lilpc_t *pc)
{
	cpu286_t *cpu = &pc->cpu;
	bus_t *bus = &pc->bus;

	/* Memory layout at 800h (per Robert Collins):
	 * 800: (unused)          806: MSW
	 * 808: (unused)          80A: TR
	 * 80C: FLAGS             80E: IP
	 * 810: LDT               812: DS (sel)
	 * 814: SS (sel)           816: CS (sel)
	 * 818: ES (sel)           81A: DI
	 * 81C: SI                 81E: BP
	 * 820: SP                 822: BX
	 * 824: DX                 826: CX
	 * 828: AX
	 * 82A: ES descriptor (base_lo, base_hi, limit, access) = 6 bytes
	 * 830: CS descriptor
	 * 836: SS descriptor
	 * 83C: DS descriptor
	 * 842: GDTR (base24, limit16) = 5 bytes? Actually 6
	 * 848: LDT descriptor
	 * 84E: IDTR
	 * 854: TSS descriptor
	 */
	cpu->msw = bus_read16(bus, 0x806);
	cpu->tr.sel = bus_read16(bus, 0x80A);
	cpu->flags = bus_read16(bus, 0x80C) | FLAGS_ALWAYS_ON;
	cpu->ip = bus_read16(bus, 0x80E);
	cpu->ldtr.sel = bus_read16(bus, 0x810);
	cpu->seg[SEG_DS].sel = bus_read16(bus, 0x812);
	cpu->seg[SEG_SS].sel = bus_read16(bus, 0x814);
	cpu->seg[SEG_CS].sel = bus_read16(bus, 0x816);
	cpu->seg[SEG_ES].sel = bus_read16(bus, 0x818);
	cpu->di = bus_read16(bus, 0x81A);
	cpu->si = bus_read16(bus, 0x81C);
	cpu->bp = bus_read16(bus, 0x81E);
	cpu->sp = bus_read16(bus, 0x820);
	cpu->bx = bus_read16(bus, 0x822);
	cpu->dx = bus_read16(bus, 0x824);
	cpu->cx = bus_read16(bus, 0x826);
	cpu->ax = bus_read16(bus, 0x828);

	/* Load descriptor caches (base24 + limit16 + access8) */
	/* ES descriptor at 82Ah */
	cpu->seg[SEG_ES].base = bus_read16(bus, 0x82A) |
		((uint32_t)bus_read8(bus, 0x82C) << 16);
	cpu->seg[SEG_ES].limit = bus_read16(bus, 0x82D);
	cpu->seg[SEG_ES].access = bus_read8(bus, 0x82F);

	/* CS descriptor at 830h */
	cpu->seg[SEG_CS].base = bus_read16(bus, 0x830) |
		((uint32_t)bus_read8(bus, 0x832) << 16);
	cpu->seg[SEG_CS].limit = bus_read16(bus, 0x833);
	cpu->seg[SEG_CS].access = bus_read8(bus, 0x835);

	/* SS descriptor at 836h */
	cpu->seg[SEG_SS].base = bus_read16(bus, 0x836) |
		((uint32_t)bus_read8(bus, 0x838) << 16);
	cpu->seg[SEG_SS].limit = bus_read16(bus, 0x839);
	cpu->seg[SEG_SS].access = bus_read8(bus, 0x83B);

	/* DS descriptor at 83Ch */
	cpu->seg[SEG_DS].base = bus_read16(bus, 0x83C) |
		((uint32_t)bus_read8(bus, 0x83E) << 16);
	cpu->seg[SEG_DS].limit = bus_read16(bus, 0x83F);
	cpu->seg[SEG_DS].access = bus_read8(bus, 0x841);

	/* GDTR at 842h */
	cpu->gdtr.base = bus_read16(bus, 0x842) |
		((uint32_t)bus_read8(bus, 0x844) << 16);
	cpu->gdtr.limit = bus_read16(bus, 0x845);

	/* LDT descriptor at 848h */
	cpu->ldtr.base = bus_read16(bus, 0x848) |
		((uint32_t)bus_read8(bus, 0x84A) << 16);
	cpu->ldtr.limit = bus_read16(bus, 0x84B);
	cpu->ldtr.access = bus_read8(bus, 0x84D);

	/* IDTR at 84Eh */
	cpu->idtr.base = bus_read16(bus, 0x84E) |
		((uint32_t)bus_read8(bus, 0x850) << 16);
	cpu->idtr.limit = bus_read16(bus, 0x851);

	/* TSS descriptor at 854h */
	cpu->tr.base = bus_read16(bus, 0x854) |
		((uint32_t)bus_read8(bus, 0x856) << 16);
	cpu->tr.limit = bus_read16(bus, 0x857);
	cpu->tr.access = bus_read8(bus, 0x859);

	cpu->halted = false;
	return 195; /* approximate cycle count */
}

/* ================================================================
 * 0F prefix handler (286 extended instructions)
 * ================================================================ */
static int handle_0f(lilpc_t *pc)
{
	cpu286_t *cpu = &pc->cpu;
	uint8_t op2 = fetch_u8(pc);

	switch (op2) {
	case 0x00: { /* Group 6: SLDT/STR/LLDT/LTR/VERR/VERW */
		modrm_t m = decode_modrm(pc);
		switch (m.reg) {
		case 0: /* SLDT */
			write_rm16(pc, &m, cpu->ldtr.sel);
			return m.is_mem ? 3 : 2;
		case 1: /* STR */
			write_rm16(pc, &m, cpu->tr.sel);
			return m.is_mem ? 3 : 2;
		case 2: /* LLDT */
			cpu->ldtr.sel = read_rm16(pc, &m);
			/* TODO: load descriptor from GDT */
			return 17;
		case 3: /* LTR */
			cpu->tr.sel = read_rm16(pc, &m);
			/* TODO: load TSS descriptor from GDT */
			return 17;
		case 4: /* VERR */
		case 5: /* VERW */
			/* TODO: verify segment access */
			cpu->flags |= FLAG_ZF; /* pretend OK */
			return 14;
		}
		break;
	}
	case 0x01: { /* Group 7: SGDT/SIDT/LGDT/LIDT/SMSW/LMSW */
		modrm_t m = decode_modrm(pc);
		switch (m.reg) {
		case 0: /* SGDT */
			write_mem16(pc, m.seg, m.ea, cpu->gdtr.limit);
			write_mem16(pc, m.seg, m.ea + 2, (uint16_t)cpu->gdtr.base);
			write_mem8(pc, m.seg, m.ea + 4, (uint8_t)(cpu->gdtr.base >> 16));
			write_mem8(pc, m.seg, m.ea + 5, 0xFF); /* 286: high byte = FFh */
			return 11;
		case 1: /* SIDT */
			write_mem16(pc, m.seg, m.ea, cpu->idtr.limit);
			write_mem16(pc, m.seg, m.ea + 2, (uint16_t)cpu->idtr.base);
			write_mem8(pc, m.seg, m.ea + 4, (uint8_t)(cpu->idtr.base >> 16));
			write_mem8(pc, m.seg, m.ea + 5, 0xFF);
			return 11;
		case 2: /* LGDT */
			cpu->gdtr.limit = read_mem16(pc, m.seg, m.ea);
			cpu->gdtr.base = read_mem16(pc, m.seg, m.ea + 2) |
				((uint32_t)read_mem8(pc, m.seg, m.ea + 4) << 16);
			return 11;
		case 3: /* LIDT */
			cpu->idtr.limit = read_mem16(pc, m.seg, m.ea);
			cpu->idtr.base = read_mem16(pc, m.seg, m.ea + 2) |
				((uint32_t)read_mem8(pc, m.seg, m.ea + 4) << 16);
			return 11;
		case 4: /* SMSW */
			write_rm16(pc, &m, cpu->msw);
			return m.is_mem ? 3 : 2;
		case 6: /* LMSW */
			cpu->msw = (cpu->msw & 0xFFF0) |
				(read_rm16(pc, &m) & 0x000F);
			/* note: cannot clear PE via LMSW */
			if (cpu->msw & MSW_PE)
				cpu->msw |= MSW_PE;
			return 3;
		}
		break;
	}
	case 0x02: { /* LAR */
		modrm_t m = decode_modrm(pc);
		/* TODO: load access rights */
		(void)read_rm16(pc, &m);
		cpu->flags &= ~FLAG_ZF; /* not valid in real mode */
		return 14;
	}
	case 0x03: { /* LSL */
		modrm_t m = decode_modrm(pc);
		(void)read_rm16(pc, &m);
		cpu->flags &= ~FLAG_ZF;
		return 14;
	}
	case 0x05: /* LOADALL (undocumented 286) */
		return do_loadall(pc);
	case 0x06: /* CLTS */
		cpu->msw &= ~MSW_TS;
		return 2;
	case 0x18: case 0x19: case 0x1A: case 0x1B:
	case 0x1C: case 0x1D: case 0x1E: case 0x1F:
		/* PREFETCH/NOP — hint instructions, consume ModR/M and ignore */
		decode_modrm(pc);
		return 3;
	default: {
		static int of_count = 0;
		if (of_count < 5)
			fprintf(stderr, "lilpc: unknown 0F %02Xh at %04X:%04X\n",
				op2, cpu->seg[SEG_CS].sel, cpu->ip - 2);
		of_count++;
		return 3;
	}
	} /* switch */
	return 3;
}

/* ================================================================
 * Main instruction decode and execute
 *
 * Returns cycle count for the instruction.
 * ================================================================ */
int cpu286_step(lilpc_t *pc)
{
	cpu286_t *cpu = &pc->cpu;
	int cycles = 0;

	/* check for pending hardware interrupt */
	if (cpu->irq_pending && (cpu->flags & FLAG_IF)) {
		int vec = pic_get_interrupt(&pc->pic);
		if (vec >= 0) {
			cpu286_interrupt(pc, vec);
			cpu->irq_pending = false;
			cycles += 23;
		}
	}

	if (cpu->halted) {
		cycles = 1;
		cpu->cycles += cycles;
		return cycles;
	}

	/* instruction trace */
	if (pc->trace) {
		uint32_t phys = (uint32_t)cpu->seg[SEG_CS].base + cpu->ip;
		fprintf(stderr, "%04X:%04X [%05X] %02X %02X %02X  "
			"AX=%04X BX=%04X CX=%04X DX=%04X "
			"SP=%04X BP=%04X SI=%04X DI=%04X "
			"F=%04X\n",
			cpu->seg[SEG_CS].sel, cpu->ip, phys,
			bus_read8(&pc->bus, phys),
			bus_read8(&pc->bus, phys + 1),
			bus_read8(&pc->bus, phys + 2),
			cpu->ax, cpu->bx, cpu->cx, cpu->dx,
			cpu->sp, cpu->bp, cpu->si, cpu->di,
			cpu->flags);
	}

	/* consume prefixes */
	cpu->seg_override = SEG_NONE;
	cpu->rep_mode = 0;
	for (;;) {
		uint8_t p = bus_read8(&pc->bus,
			(uint32_t)cpu->seg[SEG_CS].base + cpu->ip);
		switch (p) {
		case 0x26: cpu->seg_override = SEG_ES; cpu->ip++; cycles += 2; continue;
		case 0x2E: cpu->seg_override = SEG_CS; cpu->ip++; cycles += 2; continue;
		case 0x36: cpu->seg_override = SEG_SS; cpu->ip++; cycles += 2; continue;
		case 0x3E: cpu->seg_override = SEG_DS; cpu->ip++; cycles += 2; continue;
		case 0xF0: /* LOCK */ cpu->ip++; cycles += 2; continue;
		case 0xF2: cpu->rep_mode = 2; cpu->ip++; cycles += 2; continue;
		case 0xF3: cpu->rep_mode = 1; cpu->ip++; cycles += 2; continue;
		}
		break;
	}

	uint8_t op = fetch_u8(pc);

	/* -------- ALU block: opcodes 00-3D (excluding x6, x7, 0F) -------- */
	if (op < 0x40 && (op & 6) != 6 && op != 0x0F) {
		int alu_op = (op >> 3) & 7;
		int form = op & 7;
		switch (form) {
		case 0: { /* ALU r/m8, r8 */
			modrm_t m = decode_modrm(pc);
			uint8_t a = read_rm8(pc, &m);
			uint8_t b = get_reg8(cpu, m.reg);
			uint8_t r = alu8(cpu, alu_op, a, b);
			if (alu_op != 7) write_rm8(pc, &m, r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		case 1: { /* ALU r/m16, r16 */
			modrm_t m = decode_modrm(pc);
			uint16_t a = read_rm16(pc, &m);
			uint16_t b = get_reg16(cpu, m.reg);
			uint16_t r = alu16(cpu, alu_op, a, b);
			if (alu_op != 7) write_rm16(pc, &m, r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		case 2: { /* ALU r8, r/m8 */
			modrm_t m = decode_modrm(pc);
			uint8_t a = get_reg8(cpu, m.reg);
			uint8_t b = read_rm8(pc, &m);
			uint8_t r = alu8(cpu, alu_op, a, b);
			if (alu_op != 7) set_reg8(cpu, m.reg, r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		case 3: { /* ALU r16, r/m16 */
			modrm_t m = decode_modrm(pc);
			uint16_t a = get_reg16(cpu, m.reg);
			uint16_t b = read_rm16(pc, &m);
			uint16_t r = alu16(cpu, alu_op, a, b);
			if (alu_op != 7) set_reg16(cpu, m.reg, r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		case 4: { /* ALU AL, imm8 */
			uint8_t imm = fetch_u8(pc);
			uint8_t r = alu8(cpu, alu_op, cpu->al, imm);
			if (alu_op != 7) cpu->al = r;
			cycles += 3;
			break;
		}
		case 5: { /* ALU AX, imm16 */
			uint16_t imm = fetch_u16(pc);
			uint16_t r = alu16(cpu, alu_op, cpu->ax, imm);
			if (alu_op != 7) cpu->ax = r;
			cycles += 3;
			break;
		}
		}
		goto done;
	}

	switch (op) {

	/* -------- segment push/pop -------- */
	case 0x06: push16(pc, cpu->seg[SEG_ES].sel); cycles += 3; break;
	case 0x07:
		cpu->seg[SEG_ES].sel = pop16(pc);
		cpu->seg[SEG_ES].base = (uint32_t)cpu->seg[SEG_ES].sel << 4;
		cycles += 5;
		break;
	case 0x0E: push16(pc, cpu->seg[SEG_CS].sel); cycles += 3; break;
	case 0x0F: cycles += handle_0f(pc); break; /* 286 extended */
	case 0x16: push16(pc, cpu->seg[SEG_SS].sel); cycles += 3; break;
	case 0x17:
		cpu->seg[SEG_SS].sel = pop16(pc);
		cpu->seg[SEG_SS].base = (uint32_t)cpu->seg[SEG_SS].sel << 4;
		cycles += 5;
		break;
	case 0x1E: push16(pc, cpu->seg[SEG_DS].sel); cycles += 3; break;
	case 0x1F:
		cpu->seg[SEG_DS].sel = pop16(pc);
		cpu->seg[SEG_DS].base = (uint32_t)cpu->seg[SEG_DS].sel << 4;
		cycles += 5;
		break;

	/* -------- DAA/DAS/AAA/AAS -------- */
	case 0x27: { /* DAA */
		uint8_t old_al = cpu->al;
		int old_cf = cpu->flags & FLAG_CF;
		cpu->flags &= ~FLAG_CF;
		if ((cpu->al & 0x0F) > 9 || (cpu->flags & FLAG_AF)) {
			cpu->al += 6;
			cpu->flags |= FLAG_AF;
			if (old_cf || cpu->al < old_al)
				cpu->flags |= FLAG_CF;
		} else {
			cpu->flags &= ~FLAG_AF;
		}
		if (old_al > 0x99 || old_cf) {
			cpu->al += 0x60;
			cpu->flags |= FLAG_CF;
		}
		set_szp8(cpu, cpu->al);
		cycles += 3;
		break;
	}
	case 0x2F: { /* DAS */
		uint8_t old_al = cpu->al;
		int old_cf = cpu->flags & FLAG_CF;
		cpu->flags &= ~FLAG_CF;
		if ((cpu->al & 0x0F) > 9 || (cpu->flags & FLAG_AF)) {
			cpu->al -= 6;
			cpu->flags |= FLAG_AF;
			if (old_cf || cpu->al > old_al)
				cpu->flags |= FLAG_CF;
		} else {
			cpu->flags &= ~FLAG_AF;
		}
		if (old_al > 0x99 || old_cf) {
			cpu->al -= 0x60;
			cpu->flags |= FLAG_CF;
		}
		set_szp8(cpu, cpu->al);
		cycles += 3;
		break;
	}
	case 0x37: { /* AAA */
		if ((cpu->al & 0x0F) > 9 || (cpu->flags & FLAG_AF)) {
			cpu->al += 6;
			cpu->ah++;
			cpu->flags |= (FLAG_AF | FLAG_CF);
		} else {
			cpu->flags &= ~(FLAG_AF | FLAG_CF);
		}
		cpu->al &= 0x0F;
		cycles += 3;
		break;
	}
	case 0x3F: { /* AAS */
		if ((cpu->al & 0x0F) > 9 || (cpu->flags & FLAG_AF)) {
			cpu->al -= 6;
			cpu->ah--;
			cpu->flags |= (FLAG_AF | FLAG_CF);
		} else {
			cpu->flags &= ~(FLAG_AF | FLAG_CF);
		}
		cpu->al &= 0x0F;
		cycles += 3;
		break;
	}

	/* -------- INC/DEC reg16 -------- */
	case 0x40: case 0x41: case 0x42: case 0x43:
	case 0x44: case 0x45: case 0x46: case 0x47: {
		int idx = op & 7;
		uint16_t v = get_reg16(cpu, idx);
		uint32_t r = (uint32_t)v + 1;
		/* INC doesn't affect CF */
		uint16_t save_cf = cpu->flags & FLAG_CF;
		flags_add16(cpu, v, 1, r);
		cpu->flags = (cpu->flags & ~FLAG_CF) | save_cf;
		set_reg16(cpu, idx, (uint16_t)r);
		cycles += 2;
		break;
	}
	case 0x48: case 0x49: case 0x4A: case 0x4B:
	case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
		int idx = op & 7;
		uint16_t v = get_reg16(cpu, idx);
		uint32_t r = (uint32_t)v - 1;
		uint16_t save_cf = cpu->flags & FLAG_CF;
		flags_sub16(cpu, v, 1, r);
		cpu->flags = (cpu->flags & ~FLAG_CF) | save_cf;
		set_reg16(cpu, idx, (uint16_t)r);
		cycles += 2;
		break;
	}

	/* -------- PUSH/POP reg16 -------- */
	case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x54: case 0x55: case 0x56: case 0x57:
		push16(pc, get_reg16(cpu, op & 7));
		cycles += 3;
		break;
	case 0x58: case 0x59: case 0x5A: case 0x5B:
	case 0x5C: case 0x5D: case 0x5E: case 0x5F:
		set_reg16(cpu, op & 7, pop16(pc));
		cycles += 5;
		break;

	/* -------- 286: PUSHA/POPA -------- */
	case 0x60: { /* PUSHA */
		uint16_t old_sp = cpu->sp;
		push16(pc, cpu->ax);
		push16(pc, cpu->cx);
		push16(pc, cpu->dx);
		push16(pc, cpu->bx);
		push16(pc, old_sp);
		push16(pc, cpu->bp);
		push16(pc, cpu->si);
		push16(pc, cpu->di);
		cycles += 19;
		break;
	}
	case 0x61: { /* POPA */
		cpu->di = pop16(pc);
		cpu->si = pop16(pc);
		cpu->bp = pop16(pc);
		(void)pop16(pc); /* skip SP */
		cpu->bx = pop16(pc);
		cpu->dx = pop16(pc);
		cpu->cx = pop16(pc);
		cpu->ax = pop16(pc);
		cycles += 19;
		break;
	}
	case 0x62: { /* BOUND r16, m16&16 */
		modrm_t m = decode_modrm(pc);
		int16_t val = (int16_t)get_reg16(cpu, m.reg);
		int16_t lo = (int16_t)read_mem16(pc, m.seg, m.ea);
		int16_t hi = (int16_t)read_mem16(pc, m.seg, m.ea + 2);
		if (val < lo || val > hi)
			cpu286_interrupt(pc, 5); /* #BR */
		cycles += 13;
		break;
	}
	case 0x63: { /* ARPL r/m16, r16 (protected mode) */
		modrm_t m = decode_modrm(pc);
		uint16_t dst = read_rm16(pc, &m);
		uint16_t src = get_reg16(cpu, m.reg);
		if ((dst & 3) < (src & 3)) {
			dst = (dst & ~3) | (src & 3);
			write_rm16(pc, &m, dst);
			cpu->flags |= FLAG_ZF;
		} else {
			cpu->flags &= ~FLAG_ZF;
		}
		cycles += 10;
		break;
	}

	/* -------- 286: PUSH imm -------- */
	case 0x68: { /* PUSH imm16 */
		uint16_t imm = fetch_u16(pc);
		push16(pc, imm);
		cycles += 3;
		break;
	}
	case 0x6A: { /* PUSH imm8 (sign-extended) */
		int16_t imm = (int8_t)fetch_u8(pc);
		push16(pc, (uint16_t)imm);
		cycles += 3;
		break;
	}

	/* -------- 286: IMUL imm -------- */
	case 0x69: { /* IMUL r16, r/m16, imm16 */
		modrm_t m = decode_modrm(pc);
		int16_t src = (int16_t)read_rm16(pc, &m);
		int16_t imm = (int16_t)fetch_u16(pc);
		int32_t result = (int32_t)src * imm;
		set_reg16(cpu, m.reg, (uint16_t)result);
		cpu->flags &= ~(FLAG_CF | FLAG_OF);
		if (result != (int16_t)result)
			cpu->flags |= FLAG_CF | FLAG_OF;
		cycles += 21;
		break;
	}
	case 0x6B: { /* IMUL r16, r/m16, imm8 */
		modrm_t m = decode_modrm(pc);
		int16_t src = (int16_t)read_rm16(pc, &m);
		int16_t imm = (int8_t)fetch_u8(pc);
		int32_t result = (int32_t)src * imm;
		set_reg16(cpu, m.reg, (uint16_t)result);
		cpu->flags &= ~(FLAG_CF | FLAG_OF);
		if (result != (int16_t)result)
			cpu->flags |= FLAG_CF | FLAG_OF;
		cycles += 21;
		break;
	}

	/* -------- 286: INS/OUTS -------- */
	case 0x6C: { /* INSB */
		int seg = eff_seg(cpu, SEG_ES);
		uint8_t val = bus_io_read8(pc, cpu->dx);
		write_mem8(pc, seg, cpu->di, val);
		cpu->di += (cpu->flags & FLAG_DF) ? -1 : 1;
		cycles += 5;
		break;
	}
	case 0x6D: { /* INSW */
		int seg = eff_seg(cpu, SEG_ES);
		uint16_t val = bus_io_read16(pc, cpu->dx);
		write_mem16(pc, seg, cpu->di, val);
		cpu->di += (cpu->flags & FLAG_DF) ? -2 : 2;
		cycles += 5;
		break;
	}
	case 0x6E: { /* OUTSB */
		int seg = eff_seg(cpu, SEG_DS);
		uint8_t val = read_mem8(pc, seg, cpu->si);
		bus_io_write8(pc, cpu->dx, val);
		cpu->si += (cpu->flags & FLAG_DF) ? -1 : 1;
		cycles += 5;
		break;
	}
	case 0x6F: { /* OUTSW */
		int seg = eff_seg(cpu, SEG_DS);
		uint16_t val = read_mem16(pc, seg, cpu->si);
		bus_io_write16(pc, cpu->dx, val);
		cpu->si += (cpu->flags & FLAG_DF) ? -2 : 2;
		cycles += 5;
		break;
	}

	/* -------- Jcc short -------- */
	case 0x70: case 0x71: case 0x72: case 0x73:
	case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7A: case 0x7B:
	case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
		int8_t disp = (int8_t)fetch_u8(pc);
		if (eval_cc(cpu, op & 0x0F)) {
			cpu->ip += disp;
			cycles += 7;
		} else {
			cycles += 3;
		}
		break;
	}

	/* -------- Group 1: ALU r/m, imm -------- */
	case 0x80: { /* Group 1 r/m8, imm8 */
		modrm_t m = decode_modrm(pc);
		uint8_t a = read_rm8(pc, &m);
		uint8_t b = fetch_u8(pc);
		uint8_t r = alu8(cpu, m.reg, a, b);
		if (m.reg != 7) write_rm8(pc, &m, r);
		cycles += m.is_mem ? 7 : 3;
		break;
	}
	case 0x81: { /* Group 1 r/m16, imm16 */
		modrm_t m = decode_modrm(pc);
		uint16_t a = read_rm16(pc, &m);
		uint16_t b = fetch_u16(pc);
		uint16_t r = alu16(cpu, m.reg, a, b);
		if (m.reg != 7) write_rm16(pc, &m, r);
		cycles += m.is_mem ? 7 : 3;
		break;
	}
	case 0x82: /* Group 1 r/m8, imm8 (alias of 80) */
		goto case_0x80;
	case 0x83: { /* Group 1 r/m16, imm8 (sign-extended) */
		modrm_t m = decode_modrm(pc);
		uint16_t a = read_rm16(pc, &m);
		uint16_t b = (uint16_t)(int16_t)(int8_t)fetch_u8(pc);
		uint16_t r = alu16(cpu, m.reg, a, b);
		if (m.reg != 7) write_rm16(pc, &m, r);
		cycles += m.is_mem ? 7 : 3;
		break;
	}

	/* -------- TEST -------- */
	case 0x84: { /* TEST r/m8, r8 */
		modrm_t m = decode_modrm(pc);
		uint8_t r = read_rm8(pc, &m) & get_reg8(cpu, m.reg);
		flags_logic8(cpu, r);
		cycles += m.is_mem ? 6 : 2;
		break;
	}
	case 0x85: { /* TEST r/m16, r16 */
		modrm_t m = decode_modrm(pc);
		uint16_t r = read_rm16(pc, &m) & get_reg16(cpu, m.reg);
		flags_logic16(cpu, r);
		cycles += m.is_mem ? 6 : 2;
		break;
	}

	/* -------- XCHG -------- */
	case 0x86: { /* XCHG r/m8, r8 */
		modrm_t m = decode_modrm(pc);
		uint8_t a = read_rm8(pc, &m);
		uint8_t b = get_reg8(cpu, m.reg);
		write_rm8(pc, &m, b);
		set_reg8(cpu, m.reg, a);
		cycles += m.is_mem ? 5 : 3;
		break;
	}
	case 0x87: { /* XCHG r/m16, r16 */
		modrm_t m = decode_modrm(pc);
		uint16_t a = read_rm16(pc, &m);
		uint16_t b = get_reg16(cpu, m.reg);
		write_rm16(pc, &m, b);
		set_reg16(cpu, m.reg, a);
		cycles += m.is_mem ? 5 : 3;
		break;
	}

	/* -------- MOV -------- */
	case 0x88: { /* MOV r/m8, r8 */
		modrm_t m = decode_modrm(pc);
		write_rm8(pc, &m, get_reg8(cpu, m.reg));
		cycles += m.is_mem ? 3 : 2;
		break;
	}
	case 0x89: { /* MOV r/m16, r16 */
		modrm_t m = decode_modrm(pc);
		write_rm16(pc, &m, get_reg16(cpu, m.reg));
		cycles += m.is_mem ? 3 : 2;
		break;
	}
	case 0x8A: { /* MOV r8, r/m8 */
		modrm_t m = decode_modrm(pc);
		set_reg8(cpu, m.reg, read_rm8(pc, &m));
		cycles += m.is_mem ? 5 : 2;
		break;
	}
	case 0x8B: { /* MOV r16, r/m16 */
		modrm_t m = decode_modrm(pc);
		set_reg16(cpu, m.reg, read_rm16(pc, &m));
		cycles += m.is_mem ? 5 : 2;
		break;
	}
	case 0x8C: { /* MOV r/m16, Sreg */
		modrm_t m = decode_modrm(pc);
		write_rm16(pc, &m, cpu->seg[m.reg & 3].sel);
		cycles += m.is_mem ? 3 : 2;
		break;
	}
	case 0x8D: { /* LEA r16, m */
		modrm_t m = decode_modrm(pc);
		set_reg16(cpu, m.reg, m.ea);
		cycles += 3;
		break;
	}
	case 0x8E: { /* MOV Sreg, r/m16 */
		modrm_t m = decode_modrm(pc);
		int seg = m.reg & 3;
		cpu->seg[seg].sel = read_rm16(pc, &m);
		cpu->seg[seg].base = (uint32_t)cpu->seg[seg].sel << 4;
		cycles += m.is_mem ? 5 : 2;
		break;
	}
	case 0x8F: { /* POP r/m16 */
		modrm_t m = decode_modrm(pc);
		write_rm16(pc, &m, pop16(pc));
		cycles += m.is_mem ? 5 : 3;
		break;
	}

	/* -------- XCHG AX, r16 / NOP -------- */
	case 0x90: cycles += 3; break; /* NOP */
	case 0x91: case 0x92: case 0x93:
	case 0x94: case 0x95: case 0x96: case 0x97: {
		int idx = op & 7;
		uint16_t tmp = cpu->ax;
		cpu->ax = get_reg16(cpu, idx);
		set_reg16(cpu, idx, tmp);
		cycles += 3;
		break;
	}

	/* -------- CBW/CWD -------- */
	case 0x98: /* CBW */
		cpu->ax = (uint16_t)(int16_t)(int8_t)cpu->al;
		cycles += 2;
		break;
	case 0x99: /* CWD */
		cpu->dx = (cpu->ax & 0x8000) ? 0xFFFF : 0x0000;
		cycles += 2;
		break;

	/* -------- CALL far -------- */
	case 0x9A: {
		uint16_t off = fetch_u16(pc);
		uint16_t seg = fetch_u16(pc);
		push16(pc, cpu->seg[SEG_CS].sel);
		push16(pc, cpu->ip);
		cpu->ip = off;
		cpu->seg[SEG_CS].sel = seg;
		cpu->seg[SEG_CS].base = (uint32_t)seg << 4;
		cycles += 13;
		break;
	}

	case 0x9B: cycles += 3; break; /* WAIT/FWAIT */

	/* -------- PUSHF/POPF -------- */
	case 0x9C: /* PUSHF */
		push16(pc, cpu->flags | FLAGS_ALWAYS_ON);
		cycles += 3;
		break;
	case 0x9D: /* POPF */
		cpu->flags = (pop16(pc) & 0x0FFF) | FLAGS_ALWAYS_ON;
		cycles += 5;
		break;

	/* -------- SAHF/LAHF -------- */
	case 0x9E: /* SAHF */
		cpu->flags = (cpu->flags & 0xFF00) | (cpu->ah & 0xD5) | FLAGS_ALWAYS_ON;
		cycles += 2;
		break;
	case 0x9F: /* LAHF */
		cpu->ah = (uint8_t)(cpu->flags & 0xFF);
		cycles += 2;
		break;

	/* -------- MOV AL/AX, moffs -------- */
	case 0xA0: {
		uint16_t off = fetch_u16(pc);
		cpu->al = read_mem8(pc, eff_seg(cpu, SEG_DS), off);
		cycles += 5;
		break;
	}
	case 0xA1: {
		uint16_t off = fetch_u16(pc);
		cpu->ax = read_mem16(pc, eff_seg(cpu, SEG_DS), off);
		cycles += 5;
		break;
	}
	case 0xA2: {
		uint16_t off = fetch_u16(pc);
		write_mem8(pc, eff_seg(cpu, SEG_DS), off, cpu->al);
		cycles += 3;
		break;
	}
	case 0xA3: {
		uint16_t off = fetch_u16(pc);
		write_mem16(pc, eff_seg(cpu, SEG_DS), off, cpu->ax);
		cycles += 3;
		break;
	}

	/* -------- String operations -------- */
	case 0xA4: { /* MOVSB */
		int sseg = eff_seg(cpu, SEG_DS);
		int dir = (cpu->flags & FLAG_DF) ? -1 : 1;
		if (cpu->rep_mode) {
			cycles += 5;
			while (cpu->cx) {
				uint8_t v = read_mem8(pc, sseg, cpu->si);
				write_mem8(pc, SEG_ES, cpu->di, v);
				cpu->si += dir;
				cpu->di += dir;
				cpu->cx--;
				cycles += 4;
			}
		} else {
			uint8_t v = read_mem8(pc, sseg, cpu->si);
			write_mem8(pc, SEG_ES, cpu->di, v);
			cpu->si += dir;
			cpu->di += dir;
			cycles += 5;
		}
		break;
	}
	case 0xA5: { /* MOVSW */
		int sseg = eff_seg(cpu, SEG_DS);
		int dir = (cpu->flags & FLAG_DF) ? -2 : 2;
		if (cpu->rep_mode) {
			cycles += 5;
			while (cpu->cx) {
				uint16_t v = read_mem16(pc, sseg, cpu->si);
				write_mem16(pc, SEG_ES, cpu->di, v);
				cpu->si += dir;
				cpu->di += dir;
				cpu->cx--;
				cycles += 4;
			}
		} else {
			uint16_t v = read_mem16(pc, sseg, cpu->si);
			write_mem16(pc, SEG_ES, cpu->di, v);
			cpu->si += dir;
			cpu->di += dir;
			cycles += 5;
		}
		break;
	}
	case 0xA6: { /* CMPSB */
		int sseg = eff_seg(cpu, SEG_DS);
		int dir = (cpu->flags & FLAG_DF) ? -1 : 1;
		if (cpu->rep_mode) {
			cycles += 5;
			while (cpu->cx) {
				uint8_t a = read_mem8(pc, sseg, cpu->si);
				uint8_t b = read_mem8(pc, SEG_ES, cpu->di);
				uint16_t r = (uint16_t)a - b;
				flags_sub8(cpu, a, b, r);
				cpu->si += dir;
				cpu->di += dir;
				cpu->cx--;
				cycles += 9;
				/* REPZ: stop if ZF=0; REPNZ: stop if ZF=1 */
				if (cpu->rep_mode == 1 && !(cpu->flags & FLAG_ZF)) break;
				if (cpu->rep_mode == 2 && (cpu->flags & FLAG_ZF)) break;
			}
		} else {
			uint8_t a = read_mem8(pc, sseg, cpu->si);
			uint8_t b = read_mem8(pc, SEG_ES, cpu->di);
			uint16_t r = (uint16_t)a - b;
			flags_sub8(cpu, a, b, r);
			cpu->si += dir;
			cpu->di += dir;
			cycles += 8;
		}
		break;
	}
	case 0xA7: { /* CMPSW */
		int sseg = eff_seg(cpu, SEG_DS);
		int dir = (cpu->flags & FLAG_DF) ? -2 : 2;
		if (cpu->rep_mode) {
			cycles += 5;
			while (cpu->cx) {
				uint16_t a = read_mem16(pc, sseg, cpu->si);
				uint16_t b = read_mem16(pc, SEG_ES, cpu->di);
				uint32_t r = (uint32_t)a - b;
				flags_sub16(cpu, a, b, r);
				cpu->si += dir;
				cpu->di += dir;
				cpu->cx--;
				cycles += 9;
				if (cpu->rep_mode == 1 && !(cpu->flags & FLAG_ZF)) break;
				if (cpu->rep_mode == 2 && (cpu->flags & FLAG_ZF)) break;
			}
		} else {
			uint16_t a = read_mem16(pc, sseg, cpu->si);
			uint16_t b = read_mem16(pc, SEG_ES, cpu->di);
			uint32_t r = (uint32_t)a - b;
			flags_sub16(cpu, a, b, r);
			cpu->si += dir;
			cpu->di += dir;
			cycles += 8;
		}
		break;
	}

	/* -------- TEST AL/AX, imm -------- */
	case 0xA8: {
		uint8_t r = cpu->al & fetch_u8(pc);
		flags_logic8(cpu, r);
		cycles += 3;
		break;
	}
	case 0xA9: {
		uint16_t r = cpu->ax & fetch_u16(pc);
		flags_logic16(cpu, r);
		cycles += 3;
		break;
	}

	/* -------- STOSB/STOSW -------- */
	case 0xAA: { /* STOSB */
		int dir = (cpu->flags & FLAG_DF) ? -1 : 1;
		if (cpu->rep_mode) {
			cycles += 4;
			while (cpu->cx) {
				write_mem8(pc, SEG_ES, cpu->di, cpu->al);
				cpu->di += dir;
				cpu->cx--;
				cycles += 3;
			}
		} else {
			write_mem8(pc, SEG_ES, cpu->di, cpu->al);
			cpu->di += dir;
			cycles += 3;
		}
		break;
	}
	case 0xAB: { /* STOSW */
		int dir = (cpu->flags & FLAG_DF) ? -2 : 2;
		if (cpu->rep_mode) {
			cycles += 4;
			while (cpu->cx) {
				write_mem16(pc, SEG_ES, cpu->di, cpu->ax);
				cpu->di += dir;
				cpu->cx--;
				cycles += 3;
			}
		} else {
			write_mem16(pc, SEG_ES, cpu->di, cpu->ax);
			cpu->di += dir;
			cycles += 3;
		}
		break;
	}

	/* -------- LODSB/LODSW -------- */
	case 0xAC: { /* LODSB */
		int sseg = eff_seg(cpu, SEG_DS);
		int dir = (cpu->flags & FLAG_DF) ? -1 : 1;
		if (cpu->rep_mode) {
			cycles += 5;
			while (cpu->cx) {
				cpu->al = read_mem8(pc, sseg, cpu->si);
				cpu->si += dir;
				cpu->cx--;
				cycles += 4;
			}
		} else {
			cpu->al = read_mem8(pc, sseg, cpu->si);
			cpu->si += dir;
			cycles += 5;
		}
		break;
	}
	case 0xAD: { /* LODSW */
		int sseg = eff_seg(cpu, SEG_DS);
		int dir = (cpu->flags & FLAG_DF) ? -2 : 2;
		if (cpu->rep_mode) {
			cycles += 5;
			while (cpu->cx) {
				cpu->ax = read_mem16(pc, sseg, cpu->si);
				cpu->si += dir;
				cpu->cx--;
				cycles += 4;
			}
		} else {
			cpu->ax = read_mem16(pc, sseg, cpu->si);
			cpu->si += dir;
			cycles += 5;
		}
		break;
	}

	/* -------- SCASB/SCASW -------- */
	case 0xAE: { /* SCASB */
		int dir = (cpu->flags & FLAG_DF) ? -1 : 1;
		if (cpu->rep_mode) {
			cycles += 5;
			while (cpu->cx) {
				uint8_t b = read_mem8(pc, SEG_ES, cpu->di);
				uint16_t r = (uint16_t)cpu->al - b;
				flags_sub8(cpu, cpu->al, b, r);
				cpu->di += dir;
				cpu->cx--;
				cycles += 8;
				if (cpu->rep_mode == 1 && !(cpu->flags & FLAG_ZF)) break;
				if (cpu->rep_mode == 2 && (cpu->flags & FLAG_ZF)) break;
			}
		} else {
			uint8_t b = read_mem8(pc, SEG_ES, cpu->di);
			uint16_t r = (uint16_t)cpu->al - b;
			flags_sub8(cpu, cpu->al, b, r);
			cpu->di += dir;
			cycles += 7;
		}
		break;
	}
	case 0xAF: { /* SCASW */
		int dir = (cpu->flags & FLAG_DF) ? -2 : 2;
		if (cpu->rep_mode) {
			cycles += 5;
			while (cpu->cx) {
				uint16_t b = read_mem16(pc, SEG_ES, cpu->di);
				uint32_t r = (uint32_t)cpu->ax - b;
				flags_sub16(cpu, cpu->ax, b, r);
				cpu->di += dir;
				cpu->cx--;
				cycles += 8;
				if (cpu->rep_mode == 1 && !(cpu->flags & FLAG_ZF)) break;
				if (cpu->rep_mode == 2 && (cpu->flags & FLAG_ZF)) break;
			}
		} else {
			uint16_t b = read_mem16(pc, SEG_ES, cpu->di);
			uint32_t r = (uint32_t)cpu->ax - b;
			flags_sub16(cpu, cpu->ax, b, r);
			cpu->di += dir;
			cycles += 7;
		}
		break;
	}

	/* -------- MOV r8, imm8 -------- */
	case 0xB0: case 0xB1: case 0xB2: case 0xB3:
	case 0xB4: case 0xB5: case 0xB6: case 0xB7:
		set_reg8(cpu, op & 7, fetch_u8(pc));
		cycles += 2;
		break;

	/* -------- MOV r16, imm16 -------- */
	case 0xB8: case 0xB9: case 0xBA: case 0xBB:
	case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		set_reg16(cpu, op & 7, fetch_u16(pc));
		cycles += 2;
		break;

	/* -------- Group 2 shifts with imm8 (286 extension) -------- */
	case 0xC0: { /* shift r/m8, imm8 */
		modrm_t m = decode_modrm(pc);
		uint8_t val = read_rm8(pc, &m);
		int count = fetch_u8(pc);
		write_rm8(pc, &m, shift8(cpu, m.reg, val, count));
		cycles += m.is_mem ? 8 + count : 5 + count;
		break;
	}
	case 0xC1: { /* shift r/m16, imm8 */
		modrm_t m = decode_modrm(pc);
		uint16_t val = read_rm16(pc, &m);
		int count = fetch_u8(pc);
		write_rm16(pc, &m, shift16(cpu, m.reg, val, count));
		cycles += m.is_mem ? 8 + count : 5 + count;
		break;
	}

	/* -------- RET near -------- */
	case 0xC2: { /* RET imm16 */
		uint16_t imm = fetch_u16(pc);
		cpu->ip = pop16(pc);
		cpu->sp += imm;
		cycles += 11;
		break;
	}
	case 0xC3: /* RET */
		cpu->ip = pop16(pc);
		cycles += 11;
		break;

	/* -------- LES/LDS -------- */
	case 0xC4: { /* LES r16, m16:16 */
		modrm_t m = decode_modrm(pc);
		set_reg16(cpu, m.reg, read_mem16(pc, m.seg, m.ea));
		cpu->seg[SEG_ES].sel = read_mem16(pc, m.seg, m.ea + 2);
		cpu->seg[SEG_ES].base = (uint32_t)cpu->seg[SEG_ES].sel << 4;
		cycles += 7;
		break;
	}
	case 0xC5: { /* LDS r16, m16:16 */
		modrm_t m = decode_modrm(pc);
		set_reg16(cpu, m.reg, read_mem16(pc, m.seg, m.ea));
		cpu->seg[SEG_DS].sel = read_mem16(pc, m.seg, m.ea + 2);
		cpu->seg[SEG_DS].base = (uint32_t)cpu->seg[SEG_DS].sel << 4;
		cycles += 7;
		break;
	}

	/* -------- MOV r/m, imm -------- */
	case 0xC6: { /* MOV r/m8, imm8 */
		modrm_t m = decode_modrm(pc);
		write_rm8(pc, &m, fetch_u8(pc));
		cycles += m.is_mem ? 3 : 2;
		break;
	}
	case 0xC7: { /* MOV r/m16, imm16 */
		modrm_t m = decode_modrm(pc);
		write_rm16(pc, &m, fetch_u16(pc));
		cycles += m.is_mem ? 3 : 2;
		break;
	}

	/* -------- 286: ENTER/LEAVE -------- */
	case 0xC8: { /* ENTER imm16, imm8 */
		uint16_t size = fetch_u16(pc);
		uint8_t level = fetch_u8(pc) & 0x1F;
		push16(pc, cpu->bp);
		uint16_t frame = cpu->sp;
		if (level > 0) {
			for (int i = 1; i < level; i++) {
				cpu->bp -= 2;
				push16(pc, read_mem16(pc, SEG_SS, cpu->bp));
			}
			push16(pc, frame);
		}
		cpu->bp = frame;
		cpu->sp -= size;
		cycles += 11; /* approximate */
		break;
	}
	case 0xC9: /* LEAVE */
		cpu->sp = cpu->bp;
		cpu->bp = pop16(pc);
		cycles += 5;
		break;

	/* -------- RETF -------- */
	case 0xCA: { /* RETF imm16 */
		uint16_t imm = fetch_u16(pc);
		cpu->ip = pop16(pc);
		cpu->seg[SEG_CS].sel = pop16(pc);
		cpu->seg[SEG_CS].base = (uint32_t)cpu->seg[SEG_CS].sel << 4;
		cpu->sp += imm;
		cycles += 15;
		break;
	}
	case 0xCB: /* RETF */
		cpu->ip = pop16(pc);
		cpu->seg[SEG_CS].sel = pop16(pc);
		cpu->seg[SEG_CS].base = (uint32_t)cpu->seg[SEG_CS].sel << 4;
		cycles += 15;
		break;

	/* -------- INT -------- */
	case 0xCC: /* INT 3 */
		cpu286_interrupt(pc, 3);
		cycles += 23;
		break;
	case 0xCD: { /* INT imm8 */
		uint8_t num = fetch_u8(pc);
		cpu286_interrupt(pc, num);
		cycles += 23;
		break;
	}
	case 0xCE: /* INTO */
		if (cpu->flags & FLAG_OF) {
			cpu286_interrupt(pc, 4);
			cycles += 24;
		} else {
			cycles += 3;
		}
		break;
	case 0xCF: /* IRET */
		cpu->ip = pop16(pc);
		cpu->seg[SEG_CS].sel = pop16(pc);
		cpu->seg[SEG_CS].base = (uint32_t)cpu->seg[SEG_CS].sel << 4;
		cpu->flags = (pop16(pc) & 0x0FFF) | FLAGS_ALWAYS_ON;
		cycles += 17;
		break;

	/* -------- Group 2 shifts -------- */
	case 0xD0: { /* shift r/m8, 1 */
		modrm_t m = decode_modrm(pc);
		uint8_t val = read_rm8(pc, &m);
		write_rm8(pc, &m, shift8(cpu, m.reg, val, 1));
		cycles += m.is_mem ? 7 : 2;
		break;
	}
	case 0xD1: { /* shift r/m16, 1 */
		modrm_t m = decode_modrm(pc);
		uint16_t val = read_rm16(pc, &m);
		write_rm16(pc, &m, shift16(cpu, m.reg, val, 1));
		cycles += m.is_mem ? 7 : 2;
		break;
	}
	case 0xD2: { /* shift r/m8, CL */
		modrm_t m = decode_modrm(pc);
		uint8_t val = read_rm8(pc, &m);
		int count = cpu->cl;
		write_rm8(pc, &m, shift8(cpu, m.reg, val, count));
		cycles += m.is_mem ? 8 + count : 5 + count;
		break;
	}
	case 0xD3: { /* shift r/m16, CL */
		modrm_t m = decode_modrm(pc);
		uint16_t val = read_rm16(pc, &m);
		int count = cpu->cl;
		write_rm16(pc, &m, shift16(cpu, m.reg, val, count));
		cycles += m.is_mem ? 8 + count : 5 + count;
		break;
	}

	/* -------- AAM/AAD -------- */
	case 0xD4: { /* AAM imm8 */
		uint8_t base = fetch_u8(pc);
		if (base == 0) {
			cpu286_interrupt(pc, 0); /* #DE */
			cycles += 23;
		} else {
			cpu->ah = cpu->al / base;
			cpu->al = cpu->al % base;
			set_szp16(cpu, cpu->ax);
			cycles += 16;
		}
		break;
	}
	case 0xD5: { /* AAD imm8 */
		uint8_t base = fetch_u8(pc);
		cpu->al = cpu->ah * base + cpu->al;
		cpu->ah = 0;
		set_szp8(cpu, cpu->al);
		cycles += 14;
		break;
	}

	case 0xD6: /* SALC (undocumented: set AL to FF if CF, else 00) */
		cpu->al = (cpu->flags & FLAG_CF) ? 0xFF : 0x00;
		cycles += 3;
		break;

	case 0xD7: { /* XLAT */
		int seg = eff_seg(cpu, SEG_DS);
		cpu->al = read_mem8(pc, seg, cpu->bx + cpu->al);
		cycles += 5;
		break;
	}

	/* -------- ESC (FPU) -------- */
	case 0xD8: case 0xD9: case 0xDA: case 0xDB:
	case 0xDC: case 0xDD: case 0xDE: case 0xDF: {
		modrm_t m = decode_modrm(pc);
		(void)read_rm16(pc, &m); /* read operand but ignore (no FPU) */
		/* If EM bit set in MSW, trap */
		if (cpu->msw & MSW_EM) {
			cpu286_interrupt(pc, 7); /* #NM */
			cycles += 23;
		} else {
			cycles += 2;
		}
		break;
	}

	/* -------- LOOPcc / JCXZ -------- */
	case 0xE0: { /* LOOPNZ/LOOPNE */
		int8_t disp = (int8_t)fetch_u8(pc);
		cpu->cx--;
		if (cpu->cx != 0 && !(cpu->flags & FLAG_ZF)) {
			cpu->ip += disp;
			cycles += 8;
		} else {
			cycles += 4;
		}
		break;
	}
	case 0xE1: { /* LOOPZ/LOOPE */
		int8_t disp = (int8_t)fetch_u8(pc);
		cpu->cx--;
		if (cpu->cx != 0 && (cpu->flags & FLAG_ZF)) {
			cpu->ip += disp;
			cycles += 8;
		} else {
			cycles += 4;
		}
		break;
	}
	case 0xE2: { /* LOOP */
		int8_t disp = (int8_t)fetch_u8(pc);
		cpu->cx--;
		if (cpu->cx != 0) {
			cpu->ip += disp;
			cycles += 8;
		} else {
			cycles += 4;
		}
		break;
	}
	case 0xE3: { /* JCXZ */
		int8_t disp = (int8_t)fetch_u8(pc);
		if (cpu->cx == 0) {
			cpu->ip += disp;
			cycles += 8;
		} else {
			cycles += 4;
		}
		break;
	}

	/* -------- IN/OUT imm8 -------- */
	case 0xE4: { /* IN AL, imm8 */
		uint8_t port = fetch_u8(pc);
		cpu->al = bus_io_read8(pc, port);
		cycles += 5;
		break;
	}
	case 0xE5: { /* IN AX, imm8 */
		uint8_t port = fetch_u8(pc);
		cpu->ax = bus_io_read16(pc, port);
		cycles += 5;
		break;
	}
	case 0xE6: { /* OUT imm8, AL */
		uint8_t port = fetch_u8(pc);
		bus_io_write8(pc, port, cpu->al);
		cycles += 3;
		break;
	}
	case 0xE7: { /* OUT imm8, AX */
		uint8_t port = fetch_u8(pc);
		bus_io_write16(pc, port, cpu->ax);
		cycles += 3;
		break;
	}

	/* -------- CALL/JMP near -------- */
	case 0xE8: { /* CALL near */
		int16_t disp = (int16_t)fetch_u16(pc);
		push16(pc, cpu->ip);
		cpu->ip += disp;
		cycles += 7;
		break;
	}
	case 0xE9: { /* JMP near */
		int16_t disp = (int16_t)fetch_u16(pc);
		cpu->ip += disp;
		cycles += 7;
		break;
	}
	case 0xEA: { /* JMP far */
		uint16_t off = fetch_u16(pc);
		uint16_t seg = fetch_u16(pc);
		cpu->ip = off;
		cpu->seg[SEG_CS].sel = seg;
		cpu->seg[SEG_CS].base = (uint32_t)seg << 4;
		cycles += 11;
		break;
	}
	case 0xEB: { /* JMP short */
		int8_t disp = (int8_t)fetch_u8(pc);
		cpu->ip += disp;
		cycles += 7;
		break;
	}

	/* -------- IN/OUT DX -------- */
	case 0xEC: /* IN AL, DX */
		cpu->al = bus_io_read8(pc, cpu->dx);
		cycles += 5;
		break;
	case 0xED: /* IN AX, DX */
		cpu->ax = bus_io_read16(pc, cpu->dx);
		cycles += 5;
		break;
	case 0xEE: /* OUT DX, AL */
		bus_io_write8(pc, cpu->dx, cpu->al);
		cycles += 3;
		break;
	case 0xEF: /* OUT DX, AX */
		bus_io_write16(pc, cpu->dx, cpu->ax);
		cycles += 3;
		break;

	/* -------- HLT / CMC -------- */
	case 0xF4: /* HLT */
		cpu->halted = true;
		cycles += 2;
		break;
	case 0xF5: /* CMC */
		cpu->flags ^= FLAG_CF;
		cycles += 2;
		break;

	/* -------- Group 3: unary/TEST/MUL/DIV -------- */
	case 0xF6: { /* Group 3 r/m8 */
		modrm_t m = decode_modrm(pc);
		uint8_t val = read_rm8(pc, &m);
		switch (m.reg) {
		case 0: /* TEST r/m8, imm8 */
		case 1: { /* (alias) */
			uint8_t imm = fetch_u8(pc);
			flags_logic8(cpu, val & imm);
			cycles += m.is_mem ? 6 : 3;
			break;
		}
		case 2: /* NOT */
			write_rm8(pc, &m, ~val);
			cycles += m.is_mem ? 7 : 2;
			break;
		case 3: { /* NEG */
			uint16_t r = (uint16_t)0 - val;
			flags_sub8(cpu, 0, val, r);
			if (val) cpu->flags |= FLAG_CF;
			else cpu->flags &= ~FLAG_CF;
			write_rm8(pc, &m, (uint8_t)r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		case 4: { /* MUL r/m8 */
			uint16_t result = (uint16_t)cpu->al * val;
			cpu->ax = result;
			cpu->flags &= ~(FLAG_CF | FLAG_OF);
			if (cpu->ah) cpu->flags |= FLAG_CF | FLAG_OF;
			cycles += 13;
			break;
		}
		case 5: { /* IMUL r/m8 */
			int16_t result = (int16_t)(int8_t)cpu->al * (int8_t)val;
			cpu->ax = (uint16_t)result;
			cpu->flags &= ~(FLAG_CF | FLAG_OF);
			if (result != (int8_t)result)
				cpu->flags |= FLAG_CF | FLAG_OF;
			cycles += 13;
			break;
		}
		case 6: { /* DIV r/m8 */
			if (val == 0) {
				cpu286_interrupt(pc, 0);
				cycles += 23;
				break;
			}
			uint16_t num = cpu->ax;
			uint16_t quot = num / val;
			uint8_t rem = num % val;
			if (quot > 0xFF) {
				cpu286_interrupt(pc, 0);
				cycles += 23;
				break;
			}
			cpu->al = (uint8_t)quot;
			cpu->ah = rem;
			cycles += 14;
			break;
		}
		case 7: { /* IDIV r/m8 */
			if (val == 0) {
				cpu286_interrupt(pc, 0);
				cycles += 23;
				break;
			}
			int16_t num = (int16_t)cpu->ax;
			int16_t divisor = (int8_t)val;
			int16_t quot = num / divisor;
			int8_t rem = num % divisor;
			if (quot > 127 || quot < -128) {
				cpu286_interrupt(pc, 0);
				cycles += 23;
				break;
			}
			cpu->al = (uint8_t)(int8_t)quot;
			cpu->ah = (uint8_t)rem;
			cycles += 17;
			break;
		}
		}
		break;
	}
	case 0xF7: { /* Group 3 r/m16 */
		modrm_t m = decode_modrm(pc);
		uint16_t val = read_rm16(pc, &m);
		switch (m.reg) {
		case 0: /* TEST r/m16, imm16 */
		case 1: {
			uint16_t imm = fetch_u16(pc);
			flags_logic16(cpu, val & imm);
			cycles += m.is_mem ? 6 : 3;
			break;
		}
		case 2: /* NOT */
			write_rm16(pc, &m, ~val);
			cycles += m.is_mem ? 7 : 2;
			break;
		case 3: { /* NEG */
			uint32_t r = (uint32_t)0 - val;
			flags_sub16(cpu, 0, val, r);
			if (val) cpu->flags |= FLAG_CF;
			else cpu->flags &= ~FLAG_CF;
			write_rm16(pc, &m, (uint16_t)r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		case 4: { /* MUL r/m16 */
			uint32_t result = (uint32_t)cpu->ax * val;
			cpu->ax = (uint16_t)result;
			cpu->dx = (uint16_t)(result >> 16);
			cpu->flags &= ~(FLAG_CF | FLAG_OF);
			if (cpu->dx) cpu->flags |= FLAG_CF | FLAG_OF;
			cycles += 21;
			break;
		}
		case 5: { /* IMUL r/m16 */
			int32_t result = (int32_t)(int16_t)cpu->ax * (int16_t)val;
			cpu->ax = (uint16_t)result;
			cpu->dx = (uint16_t)((uint32_t)result >> 16);
			cpu->flags &= ~(FLAG_CF | FLAG_OF);
			if (result != (int16_t)result)
				cpu->flags |= FLAG_CF | FLAG_OF;
			cycles += 21;
			break;
		}
		case 6: { /* DIV r/m16 */
			if (val == 0) {
				cpu286_interrupt(pc, 0);
				cycles += 23;
				break;
			}
			uint32_t num = ((uint32_t)cpu->dx << 16) | cpu->ax;
			uint32_t quot = num / val;
			uint16_t rem = num % val;
			if (quot > 0xFFFF) {
				cpu286_interrupt(pc, 0);
				cycles += 23;
				break;
			}
			cpu->ax = (uint16_t)quot;
			cpu->dx = rem;
			cycles += 22;
			break;
		}
		case 7: { /* IDIV r/m16 */
			if (val == 0) {
				cpu286_interrupt(pc, 0);
				cycles += 23;
				break;
			}
			int32_t num = (int32_t)(((uint32_t)cpu->dx << 16) | cpu->ax);
			int32_t divisor = (int16_t)val;
			int32_t quot = num / divisor;
			int16_t rem = num % divisor;
			if (quot > 32767 || quot < -32768) {
				cpu286_interrupt(pc, 0);
				cycles += 23;
				break;
			}
			cpu->ax = (uint16_t)(int16_t)quot;
			cpu->dx = (uint16_t)rem;
			cycles += 25;
			break;
		}
		}
		break;
	}

	/* -------- flag manipulation -------- */
	case 0xF8: cpu->flags &= ~FLAG_CF; cycles += 2; break; /* CLC */
	case 0xF9: cpu->flags |= FLAG_CF; cycles += 2; break;  /* STC */
	case 0xFA: cpu->flags &= ~FLAG_IF; cycles += 2; break; /* CLI */
	case 0xFB: cpu->flags |= FLAG_IF; cycles += 2; break;  /* STI */
	case 0xFC: cpu->flags &= ~FLAG_DF; cycles += 2; break; /* CLD */
	case 0xFD: cpu->flags |= FLAG_DF; cycles += 2; break;  /* STD */

	/* -------- Group 4/5: INC/DEC/CALL/JMP/PUSH -------- */
	case 0xFE: { /* Group 4 r/m8 (INC/DEC only) */
		modrm_t m = decode_modrm(pc);
		uint8_t val = read_rm8(pc, &m);
		switch (m.reg) {
		case 0: { /* INC r/m8 */
			uint16_t r = (uint16_t)val + 1;
			uint16_t save_cf = cpu->flags & FLAG_CF;
			flags_add8(cpu, val, 1, r);
			cpu->flags = (cpu->flags & ~FLAG_CF) | save_cf;
			write_rm8(pc, &m, (uint8_t)r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		case 1: { /* DEC r/m8 */
			uint16_t r = (uint16_t)val - 1;
			uint16_t save_cf = cpu->flags & FLAG_CF;
			flags_sub8(cpu, val, 1, r);
			cpu->flags = (cpu->flags & ~FLAG_CF) | save_cf;
			write_rm8(pc, &m, (uint8_t)r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		default:
			fprintf(stderr, "lilpc: bad FE /%d at %04X:%04X\n",
				m.reg, cpu->seg[SEG_CS].sel, cpu->ip);
			break;
		}
		break;
	}
	case 0xFF: { /* Group 5 r/m16 */
		modrm_t m = decode_modrm(pc);
		switch (m.reg) {
		case 0: { /* INC r/m16 */
			uint16_t val = read_rm16(pc, &m);
			uint32_t r = (uint32_t)val + 1;
			uint16_t save_cf = cpu->flags & FLAG_CF;
			flags_add16(cpu, val, 1, r);
			cpu->flags = (cpu->flags & ~FLAG_CF) | save_cf;
			write_rm16(pc, &m, (uint16_t)r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		case 1: { /* DEC r/m16 */
			uint16_t val = read_rm16(pc, &m);
			uint32_t r = (uint32_t)val - 1;
			uint16_t save_cf = cpu->flags & FLAG_CF;
			flags_sub16(cpu, val, 1, r);
			cpu->flags = (cpu->flags & ~FLAG_CF) | save_cf;
			write_rm16(pc, &m, (uint16_t)r);
			cycles += m.is_mem ? 7 : 2;
			break;
		}
		case 2: { /* CALL r/m16 (near indirect) */
			uint16_t target = read_rm16(pc, &m);
			push16(pc, cpu->ip);
			cpu->ip = target;
			cycles += m.is_mem ? 11 : 7;
			break;
		}
		case 3: { /* CALL m16:16 (far indirect) */
			uint16_t off = read_mem16(pc, m.seg, m.ea);
			uint16_t seg = read_mem16(pc, m.seg, m.ea + 2);
			push16(pc, cpu->seg[SEG_CS].sel);
			push16(pc, cpu->ip);
			cpu->ip = off;
			cpu->seg[SEG_CS].sel = seg;
			cpu->seg[SEG_CS].base = (uint32_t)seg << 4;
			cycles += 16;
			break;
		}
		case 4: { /* JMP r/m16 (near indirect) */
			cpu->ip = read_rm16(pc, &m);
			cycles += m.is_mem ? 11 : 7;
			break;
		}
		case 5: { /* JMP m16:16 (far indirect) */
			uint16_t off = read_mem16(pc, m.seg, m.ea);
			uint16_t seg = read_mem16(pc, m.seg, m.ea + 2);
			cpu->ip = off;
			cpu->seg[SEG_CS].sel = seg;
			cpu->seg[SEG_CS].base = (uint32_t)seg << 4;
			cycles += 15;
			break;
		}
		case 6: /* PUSH r/m16 */
		case 7: /* undocumented alias for PUSH (used by 8086 DOS) */
			push16(pc, read_rm16(pc, &m));
			cycles += m.is_mem ? 5 : 3;
			break;
		}
		break;
	}

	default: {
		static int ud_count = 0;
		if (ud_count < 3) {
			uint32_t addr = cpu->seg[SEG_CS].base + cpu->ip - 1;
			fprintf(stderr, "lilpc: unknown opcode %02Xh at %04X:%04X bytes:",
				op, cpu->seg[SEG_CS].sel, cpu->ip - 1);
			for (int i = 0; i < 16; i++)
				fprintf(stderr, " %02X", bus_read8(&pc->bus, addr + i));
			fprintf(stderr, "\n");
		}
		ud_count++;
		cycles += 1;
		break;
	}

	case_0x80: { /* aliased from 0x82 */
		modrm_t m = decode_modrm(pc);
		uint8_t a = read_rm8(pc, &m);
		uint8_t b = fetch_u8(pc);
		uint8_t r = alu8(cpu, m.reg, a, b);
		if (m.reg != 7) write_rm8(pc, &m, r);
		cycles += m.is_mem ? 7 : 3;
		break;
	}
	} /* end switch */

done:
	cpu->cycles += cycles;

	/* single-step trap */
	if (cpu->trap_after) {
		cpu->trap_after = 0;
		cpu286_interrupt(pc, 1);
		cycles += 23;
	}
	if (cpu->flags & FLAG_TF)
		cpu->trap_after = 1;

	return cycles;
}
