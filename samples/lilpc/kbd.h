/* kbd.h - XT keyboard controller for lilpc */
#ifndef KBD_H
#define KBD_H

#include <stdint.h>
#include <stdbool.h>

struct lilpc;

#define KBD_BUF_SIZE	16

typedef struct kbd {
	uint8_t buf[KBD_BUF_SIZE];
	int head, tail;		/* ring buffer indices */
	uint8_t data;		/* last scancode read (port 60h) */
	uint8_t port_b;		/* port 61h: speaker/keyboard control */
	bool irq_pending;
	uint8_t dip_sw_lo;	/* port 62h low bank (port_b bit 2 clear) */
	uint8_t dip_sw_hi;	/* port 62h high bank (port_b bit 2 set) */
} kbd_t;

void kbd_init(kbd_t *kbd, struct lilpc *pc);
void kbd_press(kbd_t *kbd, struct lilpc *pc, uint8_t scancode);
void kbd_release(kbd_t *kbd, struct lilpc *pc, uint8_t scancode);
void kbd_tick(kbd_t *kbd, struct lilpc *pc);

#endif
