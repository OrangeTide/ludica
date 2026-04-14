/* serial.c - 8250 UART emulation for lilpc */
#include "serial.h"
#include "lilpc.h"
#include <string.h>

/*
 * 8250 UART register map (relative to base port):
 *   +0  RBR/THR (read/write), or DLL when DLAB=1
 *   +1  IER, or DLM when DLAB=1
 *   +2  IIR (read) / FCR (write, 16550 only)
 *   +3  LCR (line control)
 *   +4  MCR (modem control)
 *   +5  LSR (line status)
 *   +6  MSR (modem status)
 *   +7  SCR (scratch register)
 */

#define DLAB_BIT 0x80 /* LCR bit 7: Divisor Latch Access Bit */

/* LSR bits */
#define LSR_DR   0x01 /* data ready */
#define LSR_THRE 0x20 /* transmitter holding register empty */
#define LSR_TEMT 0x40 /* transmitter empty */

/* IER bits */
#define IER_RDA  0x01 /* received data available */
#define IER_THRE 0x02 /* THR empty */
#define IER_RLSI 0x04 /* receiver line status */
#define IER_MSI  0x08 /* modem status */

/* MCR bits */
#define MCR_DTR  0x01
#define MCR_RTS  0x02
#define MCR_OUT1 0x04
#define MCR_OUT2 0x08 /* required for interrupts on PC */
#define MCR_LOOP 0x10

static void uart_update_irq(uart_t *uart, lilpc_t *pc)
{
	bool irq = false;
	uint8_t iir = 0x01; /* no interrupt pending */

	if (!(uart->mcr & MCR_OUT2)) {
		/* OUT2 must be set for interrupts on PC */
		uart->iir = iir;
		return;
	}

	/* priority: line status > rx data > THR empty > modem status */
	if ((uart->ier & IER_RDA) && (uart->lsr & LSR_DR)) {
		iir = 0x04; /* received data available */
		irq = true;
	} else if ((uart->ier & IER_THRE) && (uart->lsr & LSR_THRE)) {
		iir = 0x02; /* THR empty */
		irq = true;
	}

	uart->iir = iir;

	if (irq)
		pic_raise_irq(&pc->pic, uart->irq);
	else
		pic_lower_irq(&pc->pic, uart->irq);
}

static uint8_t uart_read(lilpc_t *pc, uint16_t port)
{
	/* figure out which UART */
	uart_t *uart;
	if (port >= pc->com2.base_port && port < pc->com2.base_port + 8)
		uart = &pc->com2;
	else
		uart = &pc->com1;

	int reg = port - uart->base_port;

	switch (reg) {
	case 0: /* RBR or DLL */
		if (uart->lcr & DLAB_BIT)
			return uart->divisor & 0xFF;
		/* read receive buffer */
		if (uart->rx_head != uart->rx_tail) {
			uart->rbr = uart->rx_fifo[uart->rx_head];
			uart->rx_head = (uart->rx_head + 1) % UART_FIFO_SIZE;
			if (uart->rx_head == uart->rx_tail)
				uart->lsr &= ~LSR_DR;
		}
		uart_update_irq(uart, pc);
		return uart->rbr;

	case 1: /* IER or DLM */
		if (uart->lcr & DLAB_BIT)
			return (uart->divisor >> 8) & 0xFF;
		return uart->ier;

	case 2: /* IIR */
		return uart->iir;

	case 3: return uart->lcr;
	case 4: return uart->mcr;

	case 5: /* LSR */
	{
		uint8_t val = uart->lsr;
		/* overrun/error bits cleared on read */
		uart->lsr &= (LSR_DR | LSR_THRE | LSR_TEMT);
		return val;
	}

	case 6: /* MSR */
	{
		uint8_t val = uart->msr;
		/* loopback: MCR controls MSR */
		if (uart->mcr & MCR_LOOP) {
			val = 0;
			if (uart->mcr & MCR_RTS) val |= 0x10; /* CTS */
			if (uart->mcr & MCR_DTR) val |= 0x20; /* DSR */
		}
		/* delta bits cleared on read */
		uart->msr &= 0xF0;
		return val;
	}

	case 7: return uart->scr;
	}
	return 0xFF;
}

static void uart_write(lilpc_t *pc, uint16_t port, uint8_t val)
{
	uart_t *uart;
	if (port >= pc->com2.base_port && port < pc->com2.base_port + 8)
		uart = &pc->com2;
	else
		uart = &pc->com1;

	int reg = port - uart->base_port;

	switch (reg) {
	case 0: /* THR or DLL */
		if (uart->lcr & DLAB_BIT) {
			uart->divisor = (uart->divisor & 0xFF00) | val;
		} else {
			uart->thr = val;
			uart->lsr &= ~(LSR_THRE | LSR_TEMT);
			/* in a real UART this would shift out - we just immediately complete */
			uart->lsr |= LSR_THRE | LSR_TEMT;
			uart_update_irq(uart, pc);
		}
		break;

	case 1: /* IER or DLM */
		if (uart->lcr & DLAB_BIT)
			uart->divisor = (uart->divisor & 0x00FF) | ((uint16_t)val << 8);
		else
			uart->ier = val & 0x0F;
		uart_update_irq(uart, pc);
		break;

	case 2: /* FCR (16550) - just absorb */
		uart->fcr = val;
		break;

	case 3: uart->lcr = val; break;

	case 4:
		uart->mcr = val & 0x1F;
		uart_update_irq(uart, pc);
		break;

	case 7: uart->scr = val; break;
	}
}

void uart_init(uart_t *uart, lilpc_t *pc, uint16_t base_port, int irq)
{
	memset(uart, 0, sizeof(*uart));
	uart->base_port = base_port;
	uart->irq = irq;
	uart->lsr = LSR_THRE | LSR_TEMT; /* transmitter ready */
	uart->msr = 0x30; /* CTS + DSR asserted (connected) */
	uart->iir = 0x01; /* no interrupt pending */
	uart->divisor = 12; /* 9600 baud default */

	bus_register_io(&pc->bus, base_port, 8, uart_read, uart_write);
}

void uart_receive(uart_t *uart, lilpc_t *pc, uint8_t data)
{
	int next = (uart->rx_tail + 1) % UART_FIFO_SIZE;
	if (next == uart->rx_head) {
		/* overrun */
		uart->lsr |= 0x02; /* overrun error */
		return;
	}
	uart->rx_fifo[uart->rx_tail] = data;
	uart->rx_tail = next;
	uart->lsr |= LSR_DR;
	uart_update_irq(uart, pc);
}

bool uart_has_output(uart_t *uart)
{
	/* check if THR was written (data waiting to be read by host) */
	return !(uart->lsr & LSR_THRE) || uart->thr != 0;
}

uint8_t uart_read_output(uart_t *uart)
{
	uint8_t val = uart->thr;
	uart->thr = 0;
	return val;
}
