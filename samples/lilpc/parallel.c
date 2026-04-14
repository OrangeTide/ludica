/* parallel.c - LPT parallel port + Disney Sound Source for lilpc */
#include "parallel.h"
#include "lilpc.h"
#include <string.h>

/*
 * Standard LPT parallel port:
 *   Base+0: Data register (read/write)
 *   Base+1: Status register (read)
 *     Bit 3: nError (active low)
 *     Bit 4: Select
 *     Bit 5: Paper Out
 *     Bit 6: nAck
 *     Bit 7: Busy (active low)
 *   Base+2: Control register (write)
 *     Bit 0: Strobe
 *     Bit 1: Auto-LF
 *     Bit 2: nInit (active low)
 *     Bit 3: Select In
 *     Bit 4: IRQ enable
 *
 * Disney Sound Source / Covox Speech Thing protocol:
 *   Write audio sample to data port (base+0).
 *   Toggle strobe (control bit 0) to latch sample.
 *   DSS uses a FIFO and reports busy via status bit 7.
 */

static uint8_t lpt_read(lilpc_t *pc, uint16_t port)
{
	lpt_t *lpt = &pc->lpt1;

	int reg = port - lpt->base_port;
	switch (reg) {
	case 0: return lpt->data;
	case 1:
	{
		uint8_t status = 0xDF; /* all clear, not busy, selected */
		/* Disney Sound Source: busy if FIFO is getting full */
		if (lpt->dss_enabled) {
			int count = (lpt->audio_tail - lpt->audio_head +
				LPT_FIFO_SIZE) % LPT_FIFO_SIZE;
			if (count > LPT_FIFO_SIZE - 16)
				status &= ~0x80; /* set busy (active low inverted) */
		}
		return status;
	}
	case 2: return lpt->control;
	}
	return 0xFF;
}

static void lpt_write(lilpc_t *pc, uint16_t port, uint8_t val)
{
	lpt_t *lpt = &pc->lpt1;

	int reg = port - lpt->base_port;
	switch (reg) {
	case 0:
		lpt->data = val;
		break;
	case 1:
		/* status is read-only, ignore */
		break;
	case 2:
	{
		uint8_t old = lpt->control;
		lpt->control = val;

		/* detect strobe rising edge (bit 0: 0->1) */
		if ((val & 0x01) && !(old & 0x01)) {
			/* latch data byte into audio FIFO */
			int next = (lpt->audio_tail + 1) % LPT_FIFO_SIZE;
			if (next != lpt->audio_head) {
				lpt->audio_fifo[lpt->audio_tail] = lpt->data;
				lpt->audio_tail = next;
			}
			lpt->dss_enabled = true;
		}
		break;
	}
	}
}

void lpt_init(lpt_t *lpt, lilpc_t *pc, uint16_t base_port, int irq)
{
	memset(lpt, 0, sizeof(*lpt));
	lpt->base_port = base_port;
	lpt->irq = irq;
	lpt->status = 0xDF; /* not busy, selected */

	bus_register_io(&pc->bus, base_port, 4, lpt_read, lpt_write);
}

int lpt_audio_sample(lpt_t *lpt)
{
	if (lpt->audio_head == lpt->audio_tail)
		return -1; /* FIFO empty */

	uint8_t val = lpt->audio_fifo[lpt->audio_head];
	lpt->audio_head = (lpt->audio_head + 1) % LPT_FIFO_SIZE;
	return val;
}
