/* chipset.h - 8259A PIC, 8254 PIT, 8237 DMA for lilpc */
#ifndef CHIPSET_H
#define CHIPSET_H

#include <stdint.h>
#include <stdbool.h>

struct lilpc;

/*
 * 8259A Programmable Interrupt Controller (single, XT has one)
 * XT IRQ assignments:
 *   IRQ0 = timer (PIT ch0)
 *   IRQ1 = keyboard
 *   IRQ2 = cascade (unused on XT) / EGA retrace
 *   IRQ3 = COM2 (serial)
 *   IRQ4 = COM1 (serial)
 *   IRQ5 = hard disk (XT) / LPT2
 *   IRQ6 = floppy disk
 *   IRQ7 = LPT1
 */
typedef struct pic {
	uint8_t irr;		/* interrupt request register */
	uint8_t isr;		/* in-service register */
	uint8_t imr;		/* interrupt mask register */
	uint8_t icw[4];		/* initialization command words */
	uint8_t vector_base;	/* base interrupt vector (ICW2) */
	int icw_step;		/* ICW init sequence step */
	bool init_mode;		/* in ICW initialization sequence */
	bool read_isr;		/* OCW3: read ISR vs IRR */
	int priority;		/* lowest priority level */
} pic_t;

void pic_init(pic_t *pic, struct lilpc *pc);
void pic_raise_irq(pic_t *pic, int irq);
void pic_lower_irq(pic_t *pic, int irq);
int pic_get_interrupt(pic_t *pic);	/* returns vector or -1 */
void pic_tick(pic_t *pic, struct lilpc *pc);

/*
 * 8254 Programmable Interval Timer (3 channels)
 *   Channel 0: system timer (IRQ0)
 *   Channel 1: DRAM refresh (not emulated)
 *   Channel 2: PC speaker
 */
typedef struct {
	uint16_t count;		/* current count */
	uint16_t reload;	/* reload value */
	uint16_t latch;		/* latched count for reading */
	uint8_t mode;		/* operating mode (0-5) */
	uint8_t rw_mode;	/* 1=LSB, 2=MSB, 3=LSB then MSB */
	uint8_t bcd;		/* BCD mode flag */
	bool latched;		/* count has been latched */
	bool gate;		/* gate input */
	bool out;		/* output state */
	bool msb_next;		/* next read/write is MSB (for rw_mode 3) */
	bool reload_pending;	/* reload value written, pending load */
	bool counting;		/* actively counting */
} pit_channel_t;

typedef struct pit {
	pit_channel_t ch[3];
	uint64_t last_tick;	/* last CPU cycle we ticked */
} pit_t;

void pit_init(pit_t *pit, struct lilpc *pc);
void pit_tick(pit_t *pit, struct lilpc *pc, uint64_t cpu_cycles);

/*
 * 8237 DMA Controller (single, 4 channels, XT)
 *   Channel 0: DRAM refresh
 *   Channel 1: spare / user
 *   Channel 2: floppy disk
 *   Channel 3: hard disk (XT)
 */
typedef struct {
	uint16_t base_addr;	/* base address */
	uint16_t base_count;	/* base word count */
	uint16_t curr_addr;	/* current address */
	uint16_t curr_count;	/* current word count */
	uint8_t page;		/* page register (high bits of address) */
	uint8_t mode;		/* channel mode register */
	bool masked;		/* channel mask */
} dma_channel_t;

typedef struct dma {
	dma_channel_t ch[4];
	uint8_t status;		/* status register */
	uint8_t command;	/* command register */
	bool flipflop;		/* byte pointer flip-flop */
} dma_t;

void dma_init(dma_t *dma, struct lilpc *pc);

/* execute a DMA transfer for the given channel, returns bytes transferred */
int dma_transfer(dma_t *dma, struct lilpc *pc, int channel,
	uint8_t *buf, int count, bool to_memory);

#endif
