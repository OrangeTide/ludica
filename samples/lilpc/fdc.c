/* fdc.c - NEC 765 floppy disk controller for lilpc */
#include "fdc.h"
#include "lilpc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * NEC 765 / Intel 8272A FDC emulation.
 * The XT uses DMA channel 2 for floppy transfers.
 *
 * I/O ports (primary):
 *   3F2h: Digital Output Register (DOR) - motor/drive select/DMA/reset
 *   3F4h: Main Status Register (MSR) - read only
 *   3F5h: Data register - command/result/data FIFO
 *   3F7h: Digital Input Register (DIR) / Data Rate Select (AT only)
 */

/* debug traces controlled by pc->debug & DBG_FDC at runtime */

/* FDC command bytes (bits 4:0) */
#define FDC_CMD_READ_DATA	0x06
#define FDC_CMD_READ_DELETED	0x0C
#define FDC_CMD_WRITE_DATA	0x05
#define FDC_CMD_WRITE_DELETED	0x09
#define FDC_CMD_READ_TRACK	0x02
#define FDC_CMD_VERIFY		0x16
#define FDC_CMD_FORMAT_TRACK	0x0D
#define FDC_CMD_RECALIBRATE	0x07
#define FDC_CMD_SENSE_INT	0x08
#define FDC_CMD_SPECIFY		0x03
#define FDC_CMD_SENSE_DRIVE	0x04
#define FDC_CMD_SEEK		0x0F
#define FDC_CMD_SCAN_EQUAL	0x11
#define FDC_CMD_SCAN_LO_EQ	0x19
#define FDC_CMD_SCAN_HI_EQ	0x1D

/* command parameter counts (index by command & 0x1F) */
static const int cmd_param_count[32] = {
	[FDC_CMD_READ_DATA]    = 8,
	[FDC_CMD_READ_DELETED] = 8,
	[FDC_CMD_WRITE_DATA]   = 8,
	[FDC_CMD_WRITE_DELETED]= 8,
	[FDC_CMD_READ_TRACK]   = 8,
	[FDC_CMD_FORMAT_TRACK] = 5,
	[FDC_CMD_RECALIBRATE]  = 1,
	[FDC_CMD_SENSE_INT]    = 0,
	[FDC_CMD_SPECIFY]      = 2,
	[FDC_CMD_SENSE_DRIVE]  = 1,
	[FDC_CMD_SEEK]         = 2,
};

static void fdc_start_command(fdc_t *fdc, lilpc_t *pc);

/* auto-detect geometry from image size */
static void detect_geometry(fdc_drive_t *drv)
{
	drv->heads = 2;
	switch (drv->size) {
	case 163840:  /* 160K: 1 head, 40 cyl, 8 spt */
		drv->heads = 1;
		drv->cylinders = 40;
		drv->sectors = 8;
		break;
	case 184320:  /* 180K: 1 head, 40 cyl, 9 spt */
		drv->heads = 1;
		drv->cylinders = 40;
		drv->sectors = 9;
		break;
	case 327680:  /* 320K: 2 heads, 40 cyl, 8 spt */
		drv->cylinders = 40;
		drv->sectors = 8;
		break;
	case 368640:  /* 360K: 2 heads, 40 cyl, 9 spt */
		drv->cylinders = 40;
		drv->sectors = 9;
		break;
	case 737280:  /* 720K: 2 heads, 80 cyl, 9 spt */
		drv->cylinders = 80;
		drv->sectors = 9;
		break;
	case 1228800: /* 1.2M: 2 heads, 80 cyl, 15 spt */
		drv->cylinders = 80;
		drv->sectors = 15;
		break;
	case 1474560: /* 1.44M: 2 heads, 80 cyl, 18 spt */
		drv->cylinders = 80;
		drv->sectors = 18;
		break;
	case 2949120: /* 2.88M: 2 heads, 80 cyl, 36 spt */
		drv->cylinders = 80;
		drv->sectors = 36;
		break;
	default:
		/* guess 360K for anything else */
		drv->cylinders = 40;
		drv->sectors = 9;
		break;
	}
}

static uint8_t fdc_read(lilpc_t *pc, uint16_t port)
{
	fdc_t *fdc = &pc->fdc;

	switch (port) {
	case 0x3F4: /* Main Status Register */
		return fdc->msr;

	case 0x3F5: /* data register */
		if (fdc->phase == FDC_PHASE_RESULT) {
			if (fdc->result_pos < fdc->result_len) {
				uint8_t val = fdc->result_buf[fdc->result_pos++];
				if (fdc->result_pos >= fdc->result_len) {
					fdc->phase = FDC_PHASE_IDLE;
					fdc->msr = 0x80; /* RQM, ready */
				}
				return val;
			}
		}
		return 0xFF;

	case 0x3F7: /* Digital Input Register (AT) */
		return 0x00; /* no disk change */
	}
	return 0xFF;
}

static void fdc_write(lilpc_t *pc, uint16_t port, uint8_t val)
{
	fdc_t *fdc = &pc->fdc;

	switch (port) {
	case 0x3F2: /* Digital Output Register */
	{
		bool was_reset = !(fdc->dor & 0x04);
		fdc->dor = val;
		fdc->selected_drive = val & 0x03;

		if (was_reset && (val & 0x04)) {
			/* coming out of reset */
			fdc->phase = FDC_PHASE_IDLE;
			fdc->msr = 0x80; /* RQM */
			fdc->st[0] = 0xC0; /* polling status (reset) */
			fdc->irq_pending = true;
			if (pc->debug & DBG_FDC)
				fprintf(stderr, "FDC: reset release DOR=%02X\n", val);
			if (val & 0x08) /* DMA enable? */
				pic_raise_irq(&pc->pic, 6);
		}
		if (!(val & 0x04)) {
			/* entering reset */
			if (pc->debug & DBG_FDC)
				fprintf(stderr, "FDC: entering reset DOR=%02X\n", val);
			fdc->phase = FDC_PHASE_IDLE;
			fdc->msr = 0x00;
		}
		fdc->dma_mode = (val & 0x08) != 0;
		break;
	}

	case 0x3F5: /* data register */
		if (fdc->phase == FDC_PHASE_IDLE) {
			/* first byte = command */
			fdc->cmd_buf[0] = val;
			fdc->cmd_pos = 1;

			int cmd = val & 0x1F;
			int params = 0;
			if (cmd < 32)
				params = cmd_param_count[cmd];

			if (params == 0) {
				/* no parameters, execute immediately */
				fdc->cmd_len = 1;
				fdc_start_command(fdc, pc);
			} else {
				fdc->cmd_len = 1 + params;
				fdc->phase = FDC_PHASE_COMMAND;
				fdc->msr = 0x90; /* RQM + CB (command busy) */
			}
		} else if (fdc->phase == FDC_PHASE_COMMAND) {
			if (fdc->cmd_pos < 16)
				fdc->cmd_buf[fdc->cmd_pos++] = val;
			if (fdc->cmd_pos >= fdc->cmd_len)
				fdc_start_command(fdc, pc);
		}
		break;

	case 0x3F7: /* Data Rate Select (AT) */
		break;
	}
}

/*
 * Calculate the linear byte offset for a given C/H/S.
 */
static long chs_to_offset(fdc_drive_t *drv, int c, int h, int s)
{
	return ((long)c * drv->heads * drv->sectors +
		(long)h * drv->sectors +
		(long)(s - 1)) * FDC_SECTOR_SIZE;
}

static void fdc_start_command(fdc_t *fdc, lilpc_t *pc)
{
	int cmd = fdc->cmd_buf[0] & 0x1F;

	if (pc->debug & DBG_FDC) {
		static const char *cmd_names[] = {
			[0x02] = "READ_TRACK", [0x03] = "SPECIFY",
			[0x04] = "SENSE_DRIVE", [0x05] = "WRITE_DATA",
			[0x06] = "READ_DATA", [0x07] = "RECALIBRATE",
			[0x08] = "SENSE_INT", [0x09] = "WRITE_DEL",
			[0x0C] = "READ_DEL", [0x0D] = "FORMAT_TRACK",
			[0x0F] = "SEEK",
		};
		const char *name = (cmd < 0x10 && cmd_names[cmd]) ?
			cmd_names[cmd] : "???";
		fprintf(stderr, "FDC: cmd %02X (%s)", cmd, name);
		for (int i = 1; i < fdc->cmd_len; i++)
			fprintf(stderr, " %02X", fdc->cmd_buf[i]);
		fprintf(stderr, "\n");
	}

	switch (cmd) {
	case FDC_CMD_SENSE_INT:
	{
		/* return ST0 and current cylinder */
		fdc->result_buf[0] = fdc->st[0];
		fdc->result_buf[1] = fdc->cur_cyl[fdc->selected_drive];
		fdc->result_len = 2;
		fdc->result_pos = 0;
		fdc->phase = FDC_PHASE_RESULT;
		fdc->msr = 0xD0; /* RQM + DIO + CB */
		fdc->irq_pending = false;
		pic_lower_irq(&pc->pic, 6);
		if (pc->debug & DBG_FDC)
			fprintf(stderr, "FDC: SENSE_INT -> ST0=%02X cyl=%d\n",
				fdc->result_buf[0], fdc->result_buf[1]);
		break;
	}

	case FDC_CMD_SPECIFY:
		/* just absorb the SRT/HUT/HLT/ND parameters */
		fdc->phase = FDC_PHASE_IDLE;
		fdc->msr = 0x80;
		break;

	case FDC_CMD_SENSE_DRIVE:
	{
		int drv_num = fdc->cmd_buf[1] & 0x03;
		fdc->result_buf[0] = 0x20 | drv_num; /* ST3: ready */
		if (fdc->drive[drv_num].inserted)
			fdc->result_buf[0] |= 0x20;
		if (fdc->cur_cyl[drv_num] == 0)
			fdc->result_buf[0] |= 0x10; /* track 0 */
		fdc->result_len = 1;
		fdc->result_pos = 0;
		fdc->phase = FDC_PHASE_RESULT;
		fdc->msr = 0xD0;
		break;
	}

	case FDC_CMD_RECALIBRATE:
	{
		int drv_num = fdc->cmd_buf[1] & 0x03;
		fdc->cur_cyl[drv_num] = 0;
		fdc->st[0] = 0x20 | drv_num; /* seek end */
		fdc->irq_pending = true;
		pic_raise_irq(&pc->pic, 6);
		fdc->phase = FDC_PHASE_IDLE;
		fdc->msr = 0x80;
		break;
	}

	case FDC_CMD_SEEK:
	{
		int drv_num = fdc->cmd_buf[1] & 0x03;
		int new_cyl = fdc->cmd_buf[2];
		fdc->cur_cyl[drv_num] = new_cyl;
		fdc->st[0] = 0x20 | drv_num; /* seek end */
		fdc->irq_pending = true;
		pic_raise_irq(&pc->pic, 6);
		fdc->phase = FDC_PHASE_IDLE;
		fdc->msr = 0x80;
		break;
	}

	case FDC_CMD_READ_DATA:
	case FDC_CMD_READ_DELETED:
	{
		int drv_num = fdc->cmd_buf[1] & 0x03;
		int head    = fdc->cmd_buf[3];
		int cyl     = fdc->cmd_buf[2];
		int sector  = fdc->cmd_buf[4];
		int eot     = fdc->cmd_buf[6]; /* end of track */
		int end_sector = sector; /* updated after DMA transfer */

		fdc_drive_t *drv = &fdc->drive[drv_num];
		if (pc->debug & DBG_FDC)
			fprintf(stderr, "FDC: READ drv=%d C=%d H=%d S=%d EOT=%d inserted=%d\n",
				drv_num, cyl, head, sector, eot, drv->inserted);
		if (!drv->inserted) {
			fdc->st[0] = 0x48 | drv_num; /* abnormal, not ready */
			fdc->st[1] = 0x01; /* missing address mark */
			fdc->st[2] = 0x00;
			goto read_result;
		}

		/* read sectors from disk image.
		 * Real NEC 765 always reads at least the starting sector,
		 * then continues until EOT. EOT only limits continuation,
		 * not the initial read. */
		fdc->dma_len = 0;
		for (int s = sector; ; s++) {
			long offset = chs_to_offset(drv, cyl, head, s);
			if (offset < 0 || offset + FDC_SECTOR_SIZE > (long)drv->size)
				break;
			memcpy(fdc->dma_buf + fdc->dma_len, drv->data + offset,
				FDC_SECTOR_SIZE);
			fdc->dma_len += FDC_SECTOR_SIZE;
			if (s >= eot)
				break;
		}

		/* transfer via DMA - on real hardware, DMA's terminal count
		 * (TC) signal tells the FDC to stop reading. We emulate this
		 * by checking how many bytes dma_transfer() actually moved. */
		int xfer = 0;
		if (fdc->dma_mode && fdc->dma_len > 0) {
			xfer = dma_transfer(&pc->dma, pc, 2, fdc->dma_buf,
				fdc->dma_len, true);
		}

		/* compute ending sector from actual bytes transferred */
		end_sector = sector +
			(xfer + FDC_SECTOR_SIZE - 1) / FDC_SECTOR_SIZE;

		fdc->st[0] = 0x00 | drv_num | (head << 2);
		fdc->st[1] = 0x00;
		fdc->st[2] = 0x00;
		if (pc->debug & DBG_FDC)
			fprintf(stderr, "FDC: READ result ST0=%02X xfer=%d/%d end_sec=%d\n",
				fdc->st[0], xfer, fdc->dma_len, end_sector);

read_result:
		/* set up result phase (7 bytes) */
		fdc->result_buf[0] = fdc->st[0];
		fdc->result_buf[1] = fdc->st[1];
		fdc->result_buf[2] = fdc->st[2];
		fdc->result_buf[3] = cyl;
		fdc->result_buf[4] = head;
		fdc->result_buf[5] = end_sector; /* next sector after last transferred */
		fdc->result_buf[6] = 2; /* sector size code (512) */
		fdc->result_len = 7;
		fdc->result_pos = 0;
		fdc->phase = FDC_PHASE_RESULT;
		fdc->msr = 0xD0;
		fdc->irq_pending = true;
		pic_raise_irq(&pc->pic, 6);
		break;
	}

	case FDC_CMD_WRITE_DATA:
	case FDC_CMD_WRITE_DELETED:
	{
		int drv_num = fdc->cmd_buf[1] & 0x03;
		int head    = fdc->cmd_buf[3];
		int cyl     = fdc->cmd_buf[2];
		int sector  = fdc->cmd_buf[4];
		int eot     = fdc->cmd_buf[6];
		int end_sector = sector;

		fdc_drive_t *drv = &fdc->drive[drv_num];
		if (!drv->inserted || drv->write_protect) {
			fdc->st[0] = 0x48 | drv_num;
			fdc->st[1] = drv->write_protect ? 0x02 : 0x01;
			fdc->st[2] = 0x00;
		} else {
			/* read data from DMA, write to image */
			int total = (eot - sector + 1) * FDC_SECTOR_SIZE;
			if (fdc->dma_mode && total > 0) {
				memset(fdc->dma_buf, 0, total);
				int xfer = dma_transfer(&pc->dma, pc, 2,
					fdc->dma_buf, total, false);
				int nsec = xfer / FDC_SECTOR_SIZE;

				for (int s = 0; s < nsec; s++) {
					long offset = chs_to_offset(drv, cyl, head,
						sector + s);
					if (offset >= 0 && offset + FDC_SECTOR_SIZE <= (long)drv->size) {
						memcpy(drv->data + offset,
							fdc->dma_buf + s * FDC_SECTOR_SIZE,
							FDC_SECTOR_SIZE);
					}
				}
				end_sector = sector + nsec;
			}
			fdc->st[0] = 0x00 | drv_num | (head << 2);
			fdc->st[1] = 0x00;
			fdc->st[2] = 0x00;
		}

		fdc->result_buf[0] = fdc->st[0];
		fdc->result_buf[1] = fdc->st[1];
		fdc->result_buf[2] = fdc->st[2];
		fdc->result_buf[3] = cyl;
		fdc->result_buf[4] = head;
		fdc->result_buf[5] = end_sector;
		fdc->result_buf[6] = 2;
		fdc->result_len = 7;
		fdc->result_pos = 0;
		fdc->phase = FDC_PHASE_RESULT;
		fdc->msr = 0xD0;
		fdc->irq_pending = true;
		pic_raise_irq(&pc->pic, 6);
		break;
	}

	case FDC_CMD_FORMAT_TRACK:
	{
		int drv_num = fdc->cmd_buf[1] & 0x03;
		/* simplified: just fill track with format data (0xF6) */
		fdc_drive_t *drv = &fdc->drive[drv_num];
		int head = (fdc->cmd_buf[1] >> 2) & 1;
		int spt = fdc->cmd_buf[3]; /* sectors per track */
		int cyl = fdc->cur_cyl[drv_num];

		if (drv->inserted && !drv->write_protect) {
			for (int s = 1; s <= spt; s++) {
				long offset = chs_to_offset(drv, cyl, head, s);
				if (offset >= 0 && offset + FDC_SECTOR_SIZE <= (long)drv->size)
					memset(drv->data + offset, 0xF6, FDC_SECTOR_SIZE);
			}
		}

		fdc->st[0] = 0x00 | drv_num | (head << 2);
		fdc->st[1] = 0x00;
		fdc->st[2] = 0x00;
		fdc->result_buf[0] = fdc->st[0];
		fdc->result_buf[1] = fdc->st[1];
		fdc->result_buf[2] = fdc->st[2];
		fdc->result_buf[3] = cyl;
		fdc->result_buf[4] = head;
		fdc->result_buf[5] = spt;
		fdc->result_buf[6] = 2;
		fdc->result_len = 7;
		fdc->result_pos = 0;
		fdc->phase = FDC_PHASE_RESULT;
		fdc->msr = 0xD0;
		fdc->irq_pending = true;
		pic_raise_irq(&pc->pic, 6);
		break;
	}

	default:
		/* invalid command: return ST0 with invalid command flag */
		fdc->result_buf[0] = 0x80;
		fdc->result_len = 1;
		fdc->result_pos = 0;
		fdc->phase = FDC_PHASE_RESULT;
		fdc->msr = 0xD0;
		break;
	}
}

void fdc_init(fdc_t *fdc, lilpc_t *pc)
{
	memset(fdc, 0, sizeof(*fdc));
	fdc->msr = 0x80; /* RQM: ready for commands */
	fdc->dor = 0x0C; /* motors off, DMA enabled, not in reset */
	fdc->dma_mode = true;

	bus_register_io(&pc->bus, 0x3F0, 8, fdc_read, fdc_write);
}

int fdc_load_image(fdc_t *fdc, int drive, const char *path)
{
	if (drive < 0 || drive >= FDC_MAX_DRIVES)
		return -1;

	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "lilpc: cannot open disk image: %s\n", path);
		return -1;
	}

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);

	free(fdc->drive[drive].data);
	fdc->drive[drive].data = malloc(sz);
	if (!fdc->drive[drive].data) {
		fclose(f);
		return -1;
	}

	if (fread(fdc->drive[drive].data, 1, sz, f) != (size_t)sz) {
		fclose(f);
		free(fdc->drive[drive].data);
		fdc->drive[drive].data = NULL;
		return -1;
	}
	fclose(f);

	fdc->drive[drive].size = sz;
	fdc->drive[drive].inserted = true;
	fdc->drive[drive].write_protect = false;
	detect_geometry(&fdc->drive[drive]);

	fprintf(stderr, "lilpc: loaded disk image %s (%ld bytes, %d/%d/%d)\n",
		path, sz,
		fdc->drive[drive].cylinders,
		fdc->drive[drive].heads,
		fdc->drive[drive].sectors);

	return 0;
}

int fdc_load_image_mem(fdc_t *fdc, int drive, uint8_t *data, size_t size)
{
	if (drive < 0 || drive >= FDC_MAX_DRIVES)
		return -1;

	free(fdc->drive[drive].data);
	fdc->drive[drive].data = malloc(size);
	if (!fdc->drive[drive].data)
		return -1;

	memcpy(fdc->drive[drive].data, data, size);
	fdc->drive[drive].size = size;
	fdc->drive[drive].inserted = true;
	fdc->drive[drive].write_protect = false;
	detect_geometry(&fdc->drive[drive]);

	return 0;
}

void fdc_tick(fdc_t *fdc, lilpc_t *pc)
{
	(void)fdc;
	(void)pc;
	/* FDC operations are instantaneous in our emulation */
}
