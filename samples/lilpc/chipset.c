/* chipset.c - 8259A PIC, 8254 PIT, 8237 DMA for lilpc */
#include "chipset.h"
#include "lilpc.h"
#include <string.h>

/* ========================================================================
 * 8259A PIC - Programmable Interrupt Controller
 * XT has a single PIC at ports 20h-21h
 * ======================================================================== */

static uint8_t pic_read(lilpc_t *pc, uint16_t port)
{
	pic_t *pic = &pc->pic;
	if (port == 0x20) {
		return pic->read_isr ? pic->isr : pic->irr;
	}
	/* port 0x21: IMR */
	return pic->imr;
}

static void pic_write(lilpc_t *pc, uint16_t port, uint8_t val)
{
	pic_t *pic = &pc->pic;

	if (port == 0x20) {
		if (val & 0x10) {
			/* ICW1 */
			pic->icw[0] = val;
			pic->icw_step = 1;
			pic->init_mode = true;
			pic->imr = 0;
			pic->isr = 0;
			pic->irr = 0;
			pic->read_isr = false;
			pic->priority = 7;
		} else if (val & 0x08) {
			/* OCW3 */
			if (val & 0x02)
				pic->read_isr = (val & 0x01);
		} else {
			/* OCW2 - EOI commands */
			int eoi_type = (val >> 5) & 7;
			switch (eoi_type) {
			case 1: /* non-specific EOI */
				/* clear highest priority in-service bit */
				for (int i = 0; i < 8; i++) {
					int pri = (pic->priority + 1 + i) & 7;
					if (pic->isr & (1 << pri)) {
						pic->isr &= ~(1 << pri);
						break;
					}
				}
				break;
			case 3: /* specific EOI */
				pic->isr &= ~(1 << (val & 7));
				break;
			case 5: /* rotate on non-specific EOI */
				for (int i = 0; i < 8; i++) {
					int pri = (pic->priority + 1 + i) & 7;
					if (pic->isr & (1 << pri)) {
						pic->isr &= ~(1 << pri);
						pic->priority = pri;
						break;
					}
				}
				break;
			case 7: /* rotate on specific EOI */
				pic->isr &= ~(1 << (val & 7));
				pic->priority = val & 7;
				break;
			}
		}
		return;
	}

	/* port 0x21 */
	if (pic->init_mode) {
		pic->icw[pic->icw_step] = val;
		switch (pic->icw_step) {
		case 1: /* ICW2 - vector base */
			pic->vector_base = val & 0xF8;
			/* if ICW1 bit 1 set = single PIC (no ICW3 needed) */
			if (pic->icw[0] & 0x02)
				pic->icw_step = 3;
			else
				pic->icw_step = 2;
			break;
		case 2: /* ICW3 - cascade config */
			/* if ICW1 bit 0 set = ICW4 needed */
			if (pic->icw[0] & 0x01)
				pic->icw_step = 3;
			else
				pic->init_mode = false;
			break;
		case 3: /* ICW4 */
			pic->init_mode = false;
			break;
		}
	} else {
		/* OCW1 - interrupt mask */
		pic->imr = val;
	}
}

void pic_init(pic_t *pic, lilpc_t *pc)
{
	memset(pic, 0, sizeof(*pic));
	pic->vector_base = 0x08; /* XT default: IRQ0 = INT 08h */
	pic->imr = 0xFF; /* all masked until BIOS inits */

	bus_register_io(&pc->bus, 0x20, 2, pic_read, pic_write);
}

void pic_raise_irq(pic_t *pic, int irq)
{
	pic->irr |= (1 << irq);
}

void pic_lower_irq(pic_t *pic, int irq)
{
	pic->irr &= ~(1 << irq);
}

int pic_get_interrupt(pic_t *pic)
{
	/* find highest priority unmasked, non-in-service request */
	uint8_t pending = pic->irr & ~pic->imr;
	if (!pending)
		return -1;

	for (int i = 0; i < 8; i++) {
		int pri = (pic->priority + 1 + i) & 7;
		if (pending & (1 << pri)) {
			/* check if higher-priority interrupt is in service */
			for (int j = 0; j < i; j++) {
				int hp = (pic->priority + 1 + j) & 7;
				if (pic->isr & (1 << hp))
					return -1; /* blocked by higher priority */
			}
			pic->irr &= ~(1 << pri);
			pic->isr |= (1 << pri);
			return pic->vector_base + pri;
		}
	}
	return -1;
}

void pic_tick(pic_t *pic, lilpc_t *pc)
{
	uint8_t pending = pic->irr & ~pic->imr;
	/* check if any unmasked interrupt can preempt current ISR */
	for (int i = 0; i < 8; i++) {
		int pri = (pic->priority + 1 + i) & 7;
		if (pic->isr & (1 << pri))
			break; /* higher priority in service, no preempt */
		if (pending & (1 << pri)) {
			pc->cpu.irq_pending = true;
			return;
		}
	}
	pc->cpu.irq_pending = false;
}

/* ========================================================================
 * 8254 PIT - Programmable Interval Timer
 * Ports 40h-43h, input clock 1.193182 MHz
 * ======================================================================== */

/*
 * PIT ticks at 1.193182 MHz, CPU at 6 MHz.
 * Ratio: ~5.026 CPU cycles per PIT tick
 * We use fixed-point: PIT_DIVISOR = 6000000 / 1193182 ≈ 5.028
 * For accuracy we count in PIT ticks derived from CPU cycles.
 */

static uint8_t pit_read(lilpc_t *pc, uint16_t port)
{
	pit_t *pit = &pc->pit;
	int ch_idx = port & 3;

	if (ch_idx == 3)
		return 0xFF; /* control port is write-only */

	pit_channel_t *ch = &pit->ch[ch_idx];
	uint8_t val;

	if (ch->latched) {
		/* read latched value */
		if (!ch->msb_next || ch->rw_mode != 3) {
			val = ch->latch & 0xFF;
			if (ch->rw_mode == 3)
				ch->msb_next = true;
			else
				ch->latched = false;
		} else {
			val = (ch->latch >> 8) & 0xFF;
			ch->msb_next = false;
			ch->latched = false;
		}
	} else {
		/* read live count */
		uint16_t count = ch->count;
		if (!ch->msb_next || ch->rw_mode != 3) {
			val = count & 0xFF;
			if (ch->rw_mode == 3)
				ch->msb_next = true;
		} else {
			val = (count >> 8) & 0xFF;
			ch->msb_next = false;
		}
	}
	return val;
}

static void pit_write(lilpc_t *pc, uint16_t port, uint8_t val)
{
	pit_t *pit = &pc->pit;
	int ch_idx = port & 3;

	if (ch_idx == 3) {
		/* control word */
		int sel = (val >> 6) & 3;
		if (sel == 3) {
			/* read-back command (8254 only, not 8253) */
			/* XT has 8253, but many BIOSes expect this to work */
			for (int i = 0; i < 3; i++) {
				if (val & (2 << i)) {
					if (!(val & 0x20)) /* latch count */
						pit->ch[i].latched = true;
					/* latch status not implemented */
				}
			}
			return;
		}

		pit_channel_t *ch = &pit->ch[sel];
		int rw = (val >> 4) & 3;
		if (rw == 0) {
			/* counter latch command */
			ch->latch = ch->count;
			ch->latched = true;
			return;
		}
		ch->rw_mode = rw;
		ch->mode = (val >> 1) & 7;
		ch->bcd = val & 1;
		ch->msb_next = false;
		ch->reload_pending = false;
		ch->counting = false;
		ch->out = (ch->mode == 0) ? false : true;
		return;
	}

	/* data port */
	pit_channel_t *ch = &pit->ch[ch_idx];
	switch (ch->rw_mode) {
	case 1: /* LSB only */
		ch->reload = (ch->reload & 0xFF00) | val;
		ch->reload_pending = true;
		break;
	case 2: /* MSB only */
		ch->reload = (ch->reload & 0x00FF) | ((uint16_t)val << 8);
		ch->reload_pending = true;
		break;
	case 3: /* LSB then MSB */
		if (!ch->msb_next) {
			ch->reload = (ch->reload & 0xFF00) | val;
			ch->msb_next = true;
		} else {
			ch->reload = (ch->reload & 0x00FF) | ((uint16_t)val << 8);
			ch->msb_next = false;
			ch->reload_pending = true;
		}
		break;
	}
}

void pit_init(pit_t *pit, lilpc_t *pc)
{
	memset(pit, 0, sizeof(*pit));

	/* all channels start with output high for modes 2/3 */
	for (int i = 0; i < 3; i++) {
		pit->ch[i].gate = true;
		pit->ch[i].out = true;
	}

	bus_register_io(&pc->bus, 0x40, 4, pit_read, pit_write);
}

/*
 * Decrement a single PIT channel by one tick.
 * Returns true if the output state changed.
 */
static bool pit_tick_channel(pit_channel_t *ch)
{
	if (!ch->gate && ch->mode != 1 && ch->mode != 5)
		return false;

	if (ch->reload_pending) {
		ch->count = ch->reload ? ch->reload : 0;
		ch->reload_pending = false;
		ch->counting = true;
		if (ch->mode == 0)
			ch->out = false;
	}

	if (!ch->counting)
		return false;

	bool old_out = ch->out;

	switch (ch->mode) {
	case 0: /* interrupt on terminal count */
		if (ch->count > 0)
			ch->count--;
		if (ch->count == 0)
			ch->out = true;
		break;

	case 2: /* rate generator */
		if (ch->count > 1) {
			ch->count--;
		} else {
			ch->count = ch->reload ? ch->reload : 0;
			/* output goes low for one tick */
			ch->out = false;
		}
		if (ch->count != 1)
			ch->out = true;
		break;

	case 3: /* square wave */
	{
		uint16_t reload = ch->reload ? ch->reload : 0;
		ch->count -= 2;
		if ((int16_t)ch->count <= 0) {
			ch->out = !ch->out;
			ch->count = reload;
		}
		break;
	}

	case 1: /* one-shot */
	case 4: /* software strobe */
		if (ch->count > 0)
			ch->count--;
		if (ch->count == 0)
			ch->out = !old_out;
		break;

	case 5: /* hardware strobe */
		if (ch->counting && ch->count > 0)
			ch->count--;
		if (ch->count == 0)
			ch->out = true;
		break;
	}

	return ch->out != old_out;
}

void pit_tick(pit_t *pit, lilpc_t *pc, uint64_t cpu_cycles)
{
	/*
	 * Convert CPU cycles to PIT ticks.
	 * PIT clock = 1193182 Hz, CPU clock = 6000000 Hz
	 * We accumulate a fractional counter to stay accurate.
	 */
	uint64_t elapsed = cpu_cycles - pit->last_tick;
	pit->last_tick = cpu_cycles;

	/* PIT ticks = elapsed * 1193182 / 6000000
	 * Use integer math: ticks = elapsed * 1193182 / 6000000
	 * Simplified: roughly elapsed / 5
	 */
	static uint64_t pit_accum = 0;
	pit_accum += elapsed * PIT_CLOCK_HZ;
	uint64_t ticks = pit_accum / LILPC_CLOCK_HZ;
	pit_accum %= LILPC_CLOCK_HZ;

	for (uint64_t t = 0; t < ticks; t++) {
		/* channel 0: system timer -> IRQ0 */
		if (pit_tick_channel(&pit->ch[0])) {
			if (pit->ch[0].out)
				pic_raise_irq(&pc->pic, 0);
		}

		/* channel 1: DRAM refresh - just tick it, no real effect */
		pit_tick_channel(&pit->ch[1]);

		/* channel 2: PC speaker */
		bool spk_changed = pit_tick_channel(&pit->ch[2]);
		if (spk_changed)
			speaker_update(&pc->speaker, pit->ch[2].out);
	}
}

/* ========================================================================
 * 8237 DMA Controller
 * Ports 00h-0Fh (channels), 81h-83h (page registers)
 * ======================================================================== */

static uint8_t dma_read(lilpc_t *pc, uint16_t port)
{
	dma_t *dma = &pc->dma;

	switch (port) {
	case 0x00: case 0x02: case 0x04: case 0x06:
	{
		/* current address registers (channels 0-3) */
		int ch = port >> 1;
		uint8_t val;
		if (!dma->flipflop)
			val = dma->ch[ch].curr_addr & 0xFF;
		else
			val = (dma->ch[ch].curr_addr >> 8) & 0xFF;
		dma->flipflop = !dma->flipflop;
		return val;
	}
	case 0x01: case 0x03: case 0x05: case 0x07:
	{
		/* current count registers */
		int ch = (port - 1) >> 1;
		uint8_t val;
		if (!dma->flipflop)
			val = dma->ch[ch].curr_count & 0xFF;
		else
			val = (dma->ch[ch].curr_count >> 8) & 0xFF;
		dma->flipflop = !dma->flipflop;
		return val;
	}
	case 0x08: /* status register */
	{
		uint8_t s = dma->status;
		dma->status &= 0xF0; /* clear TC bits on read */
		return s;
	}
	}
	return 0xFF;
}

static void dma_write(lilpc_t *pc, uint16_t port, uint8_t val)
{
	dma_t *dma = &pc->dma;

	switch (port) {
	case 0x00: case 0x02: case 0x04: case 0x06:
	{
		int ch = port >> 1;
		if (!dma->flipflop) {
			dma->ch[ch].base_addr = (dma->ch[ch].base_addr & 0xFF00) | val;
			dma->ch[ch].curr_addr = dma->ch[ch].base_addr;
		} else {
			dma->ch[ch].base_addr = (dma->ch[ch].base_addr & 0x00FF) |
				((uint16_t)val << 8);
			dma->ch[ch].curr_addr = dma->ch[ch].base_addr;
		}
		dma->flipflop = !dma->flipflop;
		break;
	}
	case 0x01: case 0x03: case 0x05: case 0x07:
	{
		int ch = (port - 1) >> 1;
		if (!dma->flipflop) {
			dma->ch[ch].base_count = (dma->ch[ch].base_count & 0xFF00) | val;
			dma->ch[ch].curr_count = dma->ch[ch].base_count;
		} else {
			dma->ch[ch].base_count = (dma->ch[ch].base_count & 0x00FF) |
				((uint16_t)val << 8);
			dma->ch[ch].curr_count = dma->ch[ch].base_count;
		}
		dma->flipflop = !dma->flipflop;
		break;
	}
	case 0x08: /* command register */
		dma->command = val;
		break;
	case 0x09: /* request register */
		break; /* software requests not implemented */
	case 0x0A: /* single channel mask */
	{
		int ch = val & 3;
		dma->ch[ch].masked = (val & 4) != 0;
		break;
	}
	case 0x0B: /* mode register */
	{
		int ch = val & 3;
		dma->ch[ch].mode = val;
		break;
	}
	case 0x0C: /* clear flip-flop */
		dma->flipflop = false;
		break;
	case 0x0D: /* master clear */
		dma->flipflop = false;
		dma->status = 0;
		dma->command = 0;
		for (int i = 0; i < 4; i++)
			dma->ch[i].masked = true;
		break;
	case 0x0E: /* clear mask register */
		for (int i = 0; i < 4; i++)
			dma->ch[i].masked = false;
		break;
	case 0x0F: /* write all masks */
		for (int i = 0; i < 4; i++)
			dma->ch[i].masked = (val >> i) & 1;
		break;
	}
}

/* DMA page registers (XT: 81h=ch2, 82h=ch3, 83h=ch1) */
static uint8_t dma_page_read(lilpc_t *pc, uint16_t port)
{
	dma_t *dma = &pc->dma;
	switch (port) {
	case 0x81: return dma->ch[2].page;
	case 0x82: return dma->ch[3].page;
	case 0x83: return dma->ch[1].page;
	}
	return 0xFF;
}

static void dma_page_write(lilpc_t *pc, uint16_t port, uint8_t val)
{
	dma_t *dma = &pc->dma;
	switch (port) {
	case 0x81: dma->ch[2].page = val; break;
	case 0x82: dma->ch[3].page = val; break;
	case 0x83: dma->ch[1].page = val; break;
	}
}

void dma_init(dma_t *dma, lilpc_t *pc)
{
	memset(dma, 0, sizeof(*dma));
	for (int i = 0; i < 4; i++)
		dma->ch[i].masked = true;

	bus_register_io(&pc->bus, 0x00, 16, dma_read, dma_write);
	bus_register_io(&pc->bus, 0x81, 3, dma_page_read, dma_page_write);
}

int dma_transfer(dma_t *dma, lilpc_t *pc, int channel,
	uint8_t *buf, int count, bool to_memory)
{
	dma_channel_t *ch = &dma->ch[channel];
	if (ch->masked)
		return 0;

	int transferred = 0;
	uint32_t page_base = (uint32_t)ch->page << 16;

	while (count > 0 && ch->curr_count != 0xFFFF) {
		uint32_t addr = page_base | ch->curr_addr;

		if (to_memory) {
			bus_write8(&pc->bus, addr, *buf++);
		} else {
			*buf++ = bus_read8(&pc->bus, addr);
		}

		/* auto-increment/decrement based on mode */
		if (ch->mode & 0x20)
			ch->curr_addr--;
		else
			ch->curr_addr++;

		ch->curr_count--;
		transferred++;
		count--;
	}

	if (ch->curr_count == 0xFFFF) {
		/* terminal count reached */
		dma->status |= (1 << channel);

		/* auto-init: reload base values */
		if (ch->mode & 0x10) {
			ch->curr_addr = ch->base_addr;
			ch->curr_count = ch->base_count;
		} else {
			ch->masked = true;
		}
	}

	return transferred;
}
