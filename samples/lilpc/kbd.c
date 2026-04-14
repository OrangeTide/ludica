/* kbd.c - XT keyboard controller for lilpc */
#include "kbd.h"
#include "lilpc.h"
#include <string.h>

/*
 * XT keyboard interface:
 * Port 60h (read): scancode data
 * Port 61h: PPI port B
 *   Bit 0: PIT channel 2 gate (speaker)
 *   Bit 1: speaker enable
 *   Bit 6: keyboard clock enable (0=enabled, active low on some)
 *   Bit 7: keyboard acknowledge/clear
 *
 * When a key is pressed, the keyboard sends the make code via IRQ1.
 * When released, it sends the make code OR'd with 0x80 (break code).
 */

static int kbd_buf_count(kbd_t *kbd)
{
	return (kbd->tail - kbd->head + KBD_BUF_SIZE) % KBD_BUF_SIZE;
}

static void kbd_buf_push(kbd_t *kbd, uint8_t scancode)
{
	int next = (kbd->tail + 1) % KBD_BUF_SIZE;
	if (next == kbd->head)
		return; /* buffer full */
	kbd->buf[kbd->tail] = scancode;
	kbd->tail = next;
}

static uint8_t kbd_buf_pop(kbd_t *kbd)
{
	if (kbd->head == kbd->tail)
		return 0;
	uint8_t val = kbd->buf[kbd->head];
	kbd->head = (kbd->head + 1) % KBD_BUF_SIZE;
	return val;
}

static uint8_t kbd_read(lilpc_t *pc, uint16_t port)
{
	kbd_t *kbd = &pc->kbd;

	switch (port) {
	case 0x60:
		return kbd->data;

	case 0x61:
		return kbd->port_b;

	case 0x62: {
		/*
		 * PPI port C on XT:
		 * Bit 5: PIT channel 2 output (for speaker/timing)
		 * Bits 0-3: DIP switch bank selected by port 61h bit 2
		 *   bit 2 clear = low bank (expansion RAM config)
		 *   bit 2 set   = high bank (video mode, floppy count)
		 */
		uint8_t val = pc->pit.ch[2].out ? 0x20 : 0x00;
		if (kbd->port_b & 0x04)
			val |= kbd->dip_sw_hi & 0x0F;
		else
			val |= kbd->dip_sw_lo & 0x0F;
		return val;
	}
	}
	return 0xFF;
}

static void kbd_write(lilpc_t *pc, uint16_t port, uint8_t val)
{
	kbd_t *kbd = &pc->kbd;

	switch (port) {
	case 0x61:
	{
		uint8_t old = kbd->port_b;
		kbd->port_b = val;

		/* bit 7: keyboard clear/acknowledge */
		if ((val & 0x80) && !(old & 0x80)) {
			/* acknowledge: clear IRQ and load next scancode */
			kbd->irq_pending = false;
			pic_lower_irq(&pc->pic, 1);
		}
		if (!(val & 0x80) && (old & 0x80)) {
			/* re-enable keyboard: if data in buffer, fire IRQ */
			if (kbd_buf_count(kbd) > 0) {
				kbd->data = kbd_buf_pop(kbd);
				kbd->irq_pending = true;
				pic_raise_irq(&pc->pic, 1);
			}
		}

		/* bits 0-1: speaker control */
		pc->speaker.pit_gate = (val & 0x01) != 0;
		pc->speaker.enabled = (val & 0x02) != 0;
		pc->pit.ch[2].gate = (val & 0x01) != 0;
		break;
	}
	}
}

void kbd_init(kbd_t *kbd, lilpc_t *pc)
{
	memset(kbd, 0, sizeof(*kbd));
	kbd->port_b = 0x00;

	bus_register_io(&pc->bus, 0x60, 4, kbd_read, kbd_write);
}

void kbd_press(kbd_t *kbd, lilpc_t *pc, uint8_t scancode)
{
	if (!kbd->irq_pending) {
		/* deliver immediately */
		kbd->data = scancode;
		kbd->irq_pending = true;
		pic_raise_irq(&pc->pic, 1);
	} else {
		/* queue it */
		kbd_buf_push(kbd, scancode);
	}
}

void kbd_release(kbd_t *kbd, lilpc_t *pc, uint8_t scancode)
{
	kbd_press(kbd, pc, scancode | 0x80);
}

void kbd_tick(kbd_t *kbd, lilpc_t *pc)
{
	(void)kbd;
	(void)pc;
	/* nothing periodic needed - keyboard is interrupt-driven */
}
