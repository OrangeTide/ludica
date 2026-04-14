/* bus.c - Memory bus and I/O port dispatch */
#include "bus.h"
#include "lilpc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int bus_init(bus_t *bus, size_t ram_kb)
{
	memset(bus, 0, sizeof(*bus));
	bus->ram_size = ram_kb * 1024;

	/* allocate full 1MB + 64KB (for HMA/A20 wrap) */
	bus->ram = calloc(1, 0x110000);
	if (!bus->ram)
		return -1;

	bus->a20_enabled = false; /* A20 disabled at reset (wraps at 1MB) */
	return 0;
}

void bus_cleanup(bus_t *bus)
{
	free(bus->ram);
	free(bus->rom);
	bus->ram = NULL;
	bus->rom = NULL;
}

int bus_load_rom(bus_t *bus, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "lilpc: cannot open ROM: %s\n", path);
		return -1;
	}

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);

	free(bus->rom);
	bus->rom = malloc(sz);
	if (!bus->rom) {
		fclose(f);
		return -1;
	}

	if (fread(bus->rom, 1, sz, f) != (size_t)sz) {
		fclose(f);
		free(bus->rom);
		bus->rom = NULL;
		return -1;
	}
	fclose(f);

	bus->rom_size = sz;

	/*
	 * Map ROM to top of 1MB address space.
	 * 8KB ROM (pcxtbios): maps at FE000-FFFFF
	 * 32KB ROM: maps at F8000-FFFFF
	 * 64KB ROM: maps at F0000-FFFFF
	 */
	bus->rom_base = 0x100000 - sz;
	fprintf(stderr, "lilpc: loaded ROM %s (%ld bytes) at %05Xh\n",
		path, sz, bus->rom_base);

	/* copy ROM into RAM at its mapped address */
	memcpy(bus->ram + bus->rom_base, bus->rom, sz);

	return 0;
}

/*
 * Apply A20 masking to a physical address.
 * When A20 is disabled, bit 20 is forced to 0, causing addresses
 * above 1MB to wrap around to the base of memory.
 */
static inline uint32_t a20_mask(bus_t *bus, uint32_t addr)
{
	if (!bus->a20_enabled)
		addr &= 0xFFFFF;	/* mask off bit 20+ */
	else
		addr &= 0x10FFFF;	/* 286 has 24-bit bus, but we only need 1MB+64K */
	return addr;
}

uint8_t bus_read8(bus_t *bus, uint32_t addr)
{
	addr = a20_mask(bus, addr);
	return bus->ram[addr];
}

uint16_t bus_read16(bus_t *bus, uint32_t addr)
{
	addr = a20_mask(bus, addr);
	return bus->ram[addr] | ((uint16_t)bus->ram[addr + 1] << 8);
}

void bus_write8(bus_t *bus, uint32_t addr, uint8_t val)
{
	addr = a20_mask(bus, addr);

	/* protect ROM region from writes */
	if (addr >= bus->rom_base && addr < bus->rom_base + bus->rom_size)
		return;

	/* protect video ROM region C0000-C7FFF */
	if (addr >= 0xC0000 && addr < 0xC8000)
		return;

	bus->ram[addr] = val;

	if (bus->write_hook &&
	    addr >= bus->write_hook_base && addr < bus->write_hook_end)
		bus->write_hook(bus->write_hook_ctx, addr, val);
}

void bus_write16(bus_t *bus, uint32_t addr, uint16_t val)
{
	bus_write8(bus, addr, val & 0xFF);
	bus_write8(bus, addr + 1, val >> 8);
}

/*
 * I/O port handler registration
 */
void bus_register_io(bus_t *bus, uint16_t base, uint16_t count,
	io_read_fn read, io_write_fn write)
{
	if (bus->io_count >= IO_MAX_HANDLERS) {
		fprintf(stderr, "lilpc: too many I/O handlers\n");
		return;
	}
	io_handler_t *h = &bus->io[bus->io_count++];
	h->base = base;
	h->count = count;
	h->read = read;
	h->write = write;
}

static io_handler_t *find_io_handler(bus_t *bus, uint16_t port)
{
	for (int i = 0; i < bus->io_count; i++) {
		io_handler_t *h = &bus->io[i];
		if (port >= h->base && port < h->base + h->count)
			return h;
	}
	return NULL;
}

uint8_t bus_io_read8(lilpc_t *pc, uint16_t port)
{
	/* Bochs E9 port hack: reading returns 0xE9 to detect presence */
	if (port == 0xE9)
		return 0xE9;

	io_handler_t *h = find_io_handler(&pc->bus, port);
	if (h && h->read)
		return h->read(pc, port);
	return 0xFF; /* open bus */
}

uint16_t bus_io_read16(lilpc_t *pc, uint16_t port)
{
	return bus_io_read8(pc, port) |
		((uint16_t)bus_io_read8(pc, port + 1) << 8);
}

void bus_io_write8(lilpc_t *pc, uint16_t port, uint8_t val)
{
	/* Bochs E9 port hack: output character to debug console */
	if (port == 0xE9) {
		fputc(val, stderr);
		return;
	}

	io_handler_t *h = find_io_handler(&pc->bus, port);
	if (h && h->write)
		h->write(pc, port, val);
}

void bus_io_write16(lilpc_t *pc, uint16_t port, uint16_t val)
{
	bus_io_write8(pc, port, val & 0xFF);
	bus_io_write8(pc, port + 1, val >> 8);
}
