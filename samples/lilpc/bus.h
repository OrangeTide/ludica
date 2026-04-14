/* bus.h - Memory bus and I/O port dispatch for lilpc */
#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct lilpc;

/* I/O port handler callbacks */
typedef uint8_t (*io_read_fn)(struct lilpc *pc, uint16_t port);
typedef void (*io_write_fn)(struct lilpc *pc, uint16_t port, uint8_t val);

#define IO_MAX_HANDLERS 32

typedef struct {
	uint16_t base;
	uint16_t count;
	io_read_fn read;
	io_write_fn write;
} io_handler_t;

/* Optional callback invoked on memory writes to a watched region */
typedef void (*bus_write_hook_fn)(void *ctx, uint32_t addr, uint8_t val);

typedef struct bus {
	uint8_t *ram;		/* main RAM */
	size_t ram_size;	/* in bytes, up to 1MB for XT */
	uint8_t *rom;		/* BIOS ROM data */
	size_t rom_size;	/* ROM size in bytes */
	uint32_t rom_base;	/* physical address where ROM is mapped */
	bool a20_enabled;	/* A20 gate state */

	/* I/O port handlers */
	io_handler_t io[IO_MAX_HANDLERS];
	int io_count;

	/* memory write hook (for CGA snow, etc.) */
	bus_write_hook_fn write_hook;
	void *write_hook_ctx;
	uint32_t write_hook_base;	/* start of watched range */
	uint32_t write_hook_end;	/* end of watched range (exclusive) */
} bus_t;

int bus_init(bus_t *bus, size_t ram_kb);
void bus_cleanup(bus_t *bus);
int bus_load_rom(bus_t *bus, const char *path);

/* physical address memory access */
uint8_t bus_read8(bus_t *bus, uint32_t addr);
uint16_t bus_read16(bus_t *bus, uint32_t addr);
void bus_write8(bus_t *bus, uint32_t addr, uint8_t val);
void bus_write16(bus_t *bus, uint32_t addr, uint16_t val);

/* I/O port access */
uint8_t bus_io_read8(struct lilpc *pc, uint16_t port);
uint16_t bus_io_read16(struct lilpc *pc, uint16_t port);
void bus_io_write8(struct lilpc *pc, uint16_t port, uint8_t val);
void bus_io_write16(struct lilpc *pc, uint16_t port, uint16_t val);

/* I/O handler registration */
void bus_register_io(bus_t *bus, uint16_t base, uint16_t count,
	io_read_fn read, io_write_fn write);

#endif
