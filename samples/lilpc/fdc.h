/* fdc.h - NEC 765 floppy disk controller for lilpc */
#ifndef FDC_H
#define FDC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct lilpc;

/* floppy geometry for 1.44MB 3.5" and 360KB 5.25" */
#define FDC_SECTOR_SIZE		512
#define FDC_MAX_DRIVES		2

typedef struct {
	uint8_t *data;		/* disk image data */
	size_t size;		/* image size in bytes */
	int cylinders;
	int heads;
	int sectors;		/* sectors per track */
	bool write_protect;
	bool inserted;
} fdc_drive_t;

/* FDC command phases */
enum {
	FDC_PHASE_IDLE,
	FDC_PHASE_COMMAND,
	FDC_PHASE_EXEC,
	FDC_PHASE_RESULT,
};

typedef struct fdc {
	fdc_drive_t drive[FDC_MAX_DRIVES];

	/* controller state */
	uint8_t msr;		/* main status register */
	uint8_t dor;		/* digital output register (AT, but useful) */
	uint8_t st[4];		/* status registers ST0-ST3 */
	int phase;
	int selected_drive;

	/* current command */
	uint8_t cmd_buf[16];
	int cmd_len;
	int cmd_pos;

	/* result buffer */
	uint8_t result_buf[16];
	int result_len;
	int result_pos;

	/* DMA transfer state */
	uint8_t dma_buf[FDC_SECTOR_SIZE * 36]; /* max track */
	int dma_pos;
	int dma_len;

	/* current head position per drive */
	uint8_t cur_cyl[FDC_MAX_DRIVES];

	bool irq_pending;
	bool dma_mode;		/* true = DMA, false = PIO */
} fdc_t;

void fdc_init(fdc_t *fdc, struct lilpc *pc);
int fdc_load_image(fdc_t *fdc, int drive, const char *path);
int fdc_load_image_mem(fdc_t *fdc, int drive, uint8_t *data, size_t size);
void fdc_tick(fdc_t *fdc, struct lilpc *pc);

#endif
