/* serial.h - 8250 UART emulation for lilpc */
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdbool.h>

struct lilpc;

#define UART_FIFO_SIZE	16

typedef struct uart {
	/* registers */
	uint8_t rbr;		/* receiver buffer (read) */
	uint8_t thr;		/* transmitter holding (write) */
	uint8_t ier;		/* interrupt enable */
	uint8_t iir;		/* interrupt identification (read) */
	uint8_t fcr;		/* FIFO control (write) - 16550 */
	uint8_t lcr;		/* line control */
	uint8_t mcr;		/* modem control (RTS, DTR, etc) */
	uint8_t lsr;		/* line status */
	uint8_t msr;		/* modem status (CTS, DSR, etc) */
	uint8_t scr;		/* scratch register */
	uint16_t divisor;	/* baud rate divisor */

	/* receive FIFO */
	uint8_t rx_fifo[UART_FIFO_SIZE];
	int rx_head, rx_tail;

	/* config */
	uint16_t base_port;	/* COM1=3F8, COM2=2F8 */
	int irq;		/* COM1=IRQ4, COM2=IRQ3 */
} uart_t;

void uart_init(uart_t *uart, struct lilpc *pc,
	uint16_t base_port, int irq);

/* external interface: inject a byte into the receive buffer */
void uart_receive(uart_t *uart, struct lilpc *pc, uint8_t data);

/* check if host should read transmitted data */
bool uart_has_output(uart_t *uart);
uint8_t uart_read_output(uart_t *uart);

#endif
