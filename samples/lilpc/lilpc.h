/* lilpc.h - Top-level machine state for lilpc 286 XT emulator */
#ifndef LILPC_H
#define LILPC_H

#include "bus.h"
#include "cpu286.h"
#include "chipset.h"
#include "video.h"
#include "kbd.h"
#include "fdc.h"
#include "serial.h"
#include "parallel.h"
#include "speaker.h"

/* 286 XT clock speed: 6 MHz (some XTs ran 8 MHz turbo) */
#define LILPC_CLOCK_HZ		6000000
/* approximate cycles per frame at 60 fps */
#define LILPC_CYCLES_PER_FRAME	(LILPC_CLOCK_HZ / 60)
/* PIT input clock: 1.193182 MHz */
#define PIT_CLOCK_HZ		1193182

typedef struct lilpc {
	cpu286_t cpu;
	bus_t bus;
	pic_t pic;
	pit_t pit;
	dma_t dma;
	video_t video;
	kbd_t kbd;
	fdc_t fdc;
	uart_t com1;
	uart_t com2;
	lpt_t lpt1;
	speaker_t speaker;

	/* configuration */
	int ram_kb;		/* conventional RAM in KB (default 640) */

	/* BIOS data area pointer (for convenience) */
	/* 0040:0000 = physical 0x400 */

	/* debug */
	bool trace;		/* log instructions to stderr */
	uint64_t total_cycles;
} lilpc_t;

int lilpc_init(lilpc_t *pc, int ram_kb, bool hercules,
	const char *bios_path, const char *disk_path);
void lilpc_cleanup(lilpc_t *pc);
void lilpc_run_frame(lilpc_t *pc);	/* run one frame worth of cycles */
void lilpc_reset(lilpc_t *pc);

#endif
