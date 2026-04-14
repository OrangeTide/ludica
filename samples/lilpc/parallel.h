/* parallel.h - LPT parallel port + Disney Sound Source for lilpc */
#ifndef PARALLEL_H
#define PARALLEL_H

#include <stdint.h>
#include <stdbool.h>

struct lilpc;

#define LPT_FIFO_SIZE	256

typedef struct lpt {
	uint8_t data;		/* data register (port base+0) */
	uint8_t status;		/* status register (port base+1, read) */
	uint8_t control;	/* control register (port base+2) */

	uint16_t base_port;	/* LPT1=378h, LPT2=278h */
	int irq;		/* LPT1=IRQ7, LPT2=IRQ5 */

	/* Disney Sound Source / Convox emulation */
	bool dss_enabled;
	uint8_t audio_fifo[LPT_FIFO_SIZE];
	int audio_head, audio_tail;
} lpt_t;

void lpt_init(lpt_t *lpt, struct lilpc *pc,
	uint16_t base_port, int irq);

/* get next audio sample (8-bit unsigned), returns -1 if empty */
int lpt_audio_sample(lpt_t *lpt);

#endif
