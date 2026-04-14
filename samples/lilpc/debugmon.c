/* debugmon.c - TCP debug monitor for lilpc */
#include "debugmon.h"
#include "lilpc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void dm_send(debugmon_t *dm, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void dm_send(debugmon_t *dm, const char *fmt, ...)
{
	if (dm->client_fd < 0)
		return;
	char tmp[4096];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n > 0) {
		/* best-effort, non-blocking */
		(void)send(dm->client_fd, tmp, (size_t)n, MSG_NOSIGNAL);
	}
}

static void set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int debugmon_init(debugmon_t *dm, int port)
{
	memset(dm, 0, sizeof(*dm));
	dm->listen_fd = -1;
	dm->client_fd = -1;
	dm->port = port;

	dm->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (dm->listen_fd < 0) {
		perror("debugmon: socket");
		return -1;
	}

	int opt = 1;
	setsockopt(dm->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	set_nonblock(dm->listen_fd);

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = htons((uint16_t)port),
	};

	if (bind(dm->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("debugmon: bind");
		close(dm->listen_fd);
		dm->listen_fd = -1;
		return -1;
	}

	if (listen(dm->listen_fd, 1) < 0) {
		perror("debugmon: listen");
		close(dm->listen_fd);
		dm->listen_fd = -1;
		return -1;
	}

	fprintf(stderr, "debugmon: listening on port %d\n", port);
	return 0;
}

void debugmon_cleanup(debugmon_t *dm)
{
	if (dm->client_fd >= 0) {
		close(dm->client_fd);
		dm->client_fd = -1;
	}
	if (dm->listen_fd >= 0) {
		close(dm->listen_fd);
		dm->listen_fd = -1;
	}
}

/* ---- command handlers ---- */

static void cmd_registers(debugmon_t *dm, lilpc_t *pc)
{
	cpu286_t *cpu = &pc->cpu;
	dm_send(dm, "CS:IP=%04X:%04X  flags=%04X  halted=%d  cycles=%llu\r\n",
		cpu->seg[SEG_CS].sel, cpu->ip, cpu->flags, cpu->halted,
		(unsigned long long)cpu->cycles);
	dm_send(dm, "AX=%04X BX=%04X CX=%04X DX=%04X\r\n",
		cpu->ax, cpu->bx, cpu->cx, cpu->dx);
	dm_send(dm, "SP=%04X BP=%04X SI=%04X DI=%04X\r\n",
		cpu->sp, cpu->bp, cpu->si, cpu->di);
	dm_send(dm, "DS=%04X ES=%04X SS=%04X\r\n",
		cpu->seg[SEG_DS].sel, cpu->seg[SEG_ES].sel, cpu->seg[SEG_SS].sel);
	dm_send(dm, "MSW=%04X  GDTR=%06X/%04X  IDTR=%06X/%04X\r\n",
		cpu->msw,
		cpu->gdtr.base, cpu->gdtr.limit,
		cpu->idtr.base, cpu->idtr.limit);

	/* flags decoded */
	dm_send(dm, "flags: ");
	if (cpu->flags & FLAG_CF) dm_send(dm, "CF ");
	if (cpu->flags & FLAG_PF) dm_send(dm, "PF ");
	if (cpu->flags & FLAG_AF) dm_send(dm, "AF ");
	if (cpu->flags & FLAG_ZF) dm_send(dm, "ZF ");
	if (cpu->flags & FLAG_SF) dm_send(dm, "SF ");
	if (cpu->flags & FLAG_TF) dm_send(dm, "TF ");
	if (cpu->flags & FLAG_IF) dm_send(dm, "IF ");
	if (cpu->flags & FLAG_DF) dm_send(dm, "DF ");
	if (cpu->flags & FLAG_OF) dm_send(dm, "OF ");
	dm_send(dm, "\r\n");

	/* show bytes at CS:IP */
	uint32_t addr = ((uint32_t)cpu->seg[SEG_CS].sel << 4) + cpu->ip;
	dm_send(dm, "code:");
	for (int i = 0; i < 16; i++)
		dm_send(dm, " %02X", bus_read8(&pc->bus, addr + i));
	dm_send(dm, "\r\n");
}

static int parse_segoff(const char *s, uint16_t *seg, uint16_t *off)
{
	/* parse SSSS:OOOO */
	char *colon = strchr(s, ':');
	if (!colon) return -1;
	*seg = (uint16_t)strtoul(s, NULL, 16);
	*off = (uint16_t)strtoul(colon + 1, NULL, 16);
	return 0;
}

static void cmd_memory(debugmon_t *dm, lilpc_t *pc, const char *args)
{
	uint16_t seg, off;
	int len = 128; /* default dump length */

	/* skip whitespace */
	while (*args == ' ') args++;

	if (parse_segoff(args, &seg, &off) != 0) {
		dm_send(dm, "usage: m <seg:off> [len]\r\n");
		return;
	}

	/* check for optional length */
	const char *space = strchr(args, ' ');
	if (space) {
		while (*space == ' ') space++;
		if (*space)
			len = (int)strtoul(space, NULL, 0);
	}
	if (len <= 0) len = 128;
	if (len > 65536) len = 65536;

	uint32_t base = ((uint32_t)seg << 4) + off;
	for (int i = 0; i < len; i += 16) {
		uint32_t row = base + i;
		dm_send(dm, "%05X  ", row & 0xFFFFF);
		/* hex */
		for (int j = 0; j < 16 && (i + j) < len; j++)
			dm_send(dm, "%02X ", bus_read8(&pc->bus, row + j));
		/* pad if short row */
		for (int j = len - i; j < 16; j++)
			dm_send(dm, "   ");
		dm_send(dm, " ");
		/* ASCII */
		for (int j = 0; j < 16 && (i + j) < len; j++) {
			uint8_t ch = bus_read8(&pc->bus, row + j);
			dm_send(dm, "%c", (ch >= 0x20 && ch < 0x7F) ? ch : '.');
		}
		dm_send(dm, "\r\n");
	}
}

static void cmd_breakpoint(debugmon_t *dm, const char *args)
{
	uint16_t seg, off;

	while (*args == ' ') args++;

	/* no args = list breakpoints */
	if (!*args) {
		int count = 0;
		for (int i = 0; i < DEBUGMON_MAX_BREAKPOINTS; i++) {
			if (dm->bp[i].active) {
				dm_send(dm, "bp %d: %04X:%04X\r\n",
					i, dm->bp[i].seg, dm->bp[i].off);
				count++;
			}
		}
		if (!count)
			dm_send(dm, "no breakpoints set\r\n");
		return;
	}

	/* "clear" or "c" to clear all */
	if (args[0] == 'c') {
		for (int i = 0; i < DEBUGMON_MAX_BREAKPOINTS; i++)
			dm->bp[i].active = false;
		dm->bp_count = 0;
		dm_send(dm, "all breakpoints cleared\r\n");
		return;
	}

	/* "d <n>" to delete one */
	if (args[0] == 'd') {
		args++;
		while (*args == ' ') args++;
		int n = atoi(args);
		if (n >= 0 && n < DEBUGMON_MAX_BREAKPOINTS && dm->bp[n].active) {
			dm->bp[n].active = false;
			dm->bp_count--;
			dm_send(dm, "bp %d deleted\r\n", n);
		} else {
			dm_send(dm, "invalid breakpoint number\r\n");
		}
		return;
	}

	if (parse_segoff(args, &seg, &off) != 0) {
		dm_send(dm, "usage: bp [<seg:off>|c|d <n>]\r\n");
		return;
	}

	/* find a free slot */
	for (int i = 0; i < DEBUGMON_MAX_BREAKPOINTS; i++) {
		if (!dm->bp[i].active) {
			dm->bp[i].seg = seg;
			dm->bp[i].off = off;
			dm->bp[i].active = true;
			dm->bp_count++;
			dm_send(dm, "bp %d set at %04X:%04X\r\n", i, seg, off);
			return;
		}
	}
	dm_send(dm, "no free breakpoint slots\r\n");
}

static void cmd_textbuf(debugmon_t *dm, lilpc_t *pc)
{
	uint32_t base = 0xB8000;
	int cols = 80;
	int rows = 25;

	for (int row = 0; row < rows; row++) {
		int last_nonspace = -1;
		for (int col = 0; col < cols; col++) {
			uint32_t addr = base + (row * cols + col) * 2;
			uint8_t ch = pc->bus.ram[addr];
			if (ch == 0) ch = ' ';
			if (ch != ' ') last_nonspace = col;
		}
		for (int col = 0; col <= last_nonspace; col++) {
			uint32_t addr = base + (row * cols + col) * 2;
			uint8_t ch = pc->bus.ram[addr];
			if (ch == 0) ch = ' ';
			if (ch < 0x20 || ch == 0x7F) ch = '.';
			dm_send(dm, "%c", ch);
		}
		dm_send(dm, "\r\n");
	}
}

static void cmd_patch(debugmon_t *dm, lilpc_t *pc, const char *args)
{
	uint16_t seg, off;

	while (*args == ' ') args++;

	if (parse_segoff(args, &seg, &off) != 0) {
		dm_send(dm, "usage: p <seg:off> <hex...> .\r\n");
		return;
	}

	/* advance past seg:off */
	while (*args && *args != ' ') args++;
	while (*args == ' ') args++;

	uint32_t base = ((uint32_t)seg << 4) + off;
	int count = 0;
	int nibble = -1; /* -1 = no pending nibble */

	for (const char *p = args; *p; p++) {
		if (*p == '.') break; /* terminator */
		if (isspace((unsigned char)*p)) continue;

		int val;
		if (*p >= '0' && *p <= '9') val = *p - '0';
		else if (*p >= 'a' && *p <= 'f') val = *p - 'a' + 10;
		else if (*p >= 'A' && *p <= 'F') val = *p - 'A' + 10;
		else {
			dm_send(dm, "bad hex char '%c'\r\n", *p);
			return;
		}

		if (nibble < 0) {
			nibble = val;
		} else {
			uint8_t byte = (uint8_t)((nibble << 4) | val);
			bus_write8(&pc->bus, base + count, byte);
			count++;
			nibble = -1;
		}
	}

	if (nibble >= 0) {
		/* trailing single nibble — treat as 0x_0 */
		bus_write8(&pc->bus, base + count, (uint8_t)(nibble << 4));
		count++;
	}

	dm_send(dm, "patched %d bytes at %04X:%04X\r\n", count, seg, off);
}

/* process one complete line of input */
static void process_line(debugmon_t *dm, lilpc_t *pc, char *line)
{
	/* trim leading whitespace */
	while (*line == ' ' || *line == '\t') line++;

	/* trim trailing CR/LF/space */
	int len = (int)strlen(line);
	while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
	       line[len - 1] == ' '))
		line[--len] = '\0';

	if (!*line) return;

	switch (line[0]) {
	case 'r': /* registers */
		cmd_registers(dm, pc);
		break;
	case 'm': /* memory dump */
		cmd_memory(dm, pc, line + 1);
		break;
	case 'b': /* breakpoint */
		if (line[1] == 'p')
			cmd_breakpoint(dm, line + 2);
		else
			dm_send(dm, "unknown command\r\n");
		break;
	case 's': /* single step */
		dm->step_one = true;
		dm->paused = false;
		break;
	case 'g': /* go / continue */
		dm->paused = false;
		dm->step_one = false;
		dm_send(dm, "running\r\n");
		break;
	case 't': /* text buffer */
		cmd_textbuf(dm, pc);
		break;
	case 'p': /* patch memory */
		cmd_patch(dm, pc, line + 1);
		break;
	case 'h': /* help */
	case '?':
		dm_send(dm, "commands:\r\n");
		dm_send(dm, "  r            - dump registers\r\n");
		dm_send(dm, "  m seg:off [n]- dump memory (default 128 bytes)\r\n");
		dm_send(dm, "  bp seg:off   - set breakpoint\r\n");
		dm_send(dm, "  bp           - list breakpoints\r\n");
		dm_send(dm, "  bp c         - clear all breakpoints\r\n");
		dm_send(dm, "  bp d <n>     - delete breakpoint\r\n");
		dm_send(dm, "  s            - single step\r\n");
		dm_send(dm, "  g            - go (continue)\r\n");
		dm_send(dm, "  t            - dump text buffer\r\n");
		dm_send(dm, "  p seg:off hex.. . - patch memory\r\n");
		dm_send(dm, "  h or ?       - this help\r\n");
		break;
	default:
		dm_send(dm, "unknown command '%c' (h for help)\r\n", line[0]);
		break;
	}
}

void dm_notify_break(debugmon_t *dm, lilpc_t *pc)
{
	cpu286_t *cpu = &pc->cpu;
	dm_send(dm, "break at %04X:%04X\r\n",
		cpu->seg[SEG_CS].sel, cpu->ip);
	/* show code bytes */
	uint32_t addr = ((uint32_t)cpu->seg[SEG_CS].sel << 4) + cpu->ip;
	dm_send(dm, "code:");
	for (int i = 0; i < 16; i++)
		dm_send(dm, " %02X", bus_read8(&pc->bus, addr + i));
	dm_send(dm, "\r\n");
}

void dm_notify_step(debugmon_t *dm, lilpc_t *pc)
{
	cpu286_t *cpu = &pc->cpu;
	dm_send(dm, "step %04X:%04X",
		cpu->seg[SEG_CS].sel, cpu->ip);
	/* show code bytes */
	uint32_t addr = ((uint32_t)cpu->seg[SEG_CS].sel << 4) + cpu->ip;
	dm_send(dm, "  code:");
	for (int i = 0; i < 8; i++)
		dm_send(dm, " %02X", bus_read8(&pc->bus, addr + i));
	dm_send(dm, "\r\n");
}

bool debugmon_check_bp(debugmon_t *dm, uint16_t seg, uint16_t off)
{
	for (int i = 0; i < DEBUGMON_MAX_BREAKPOINTS; i++) {
		if (dm->bp[i].active &&
		    dm->bp[i].seg == seg && dm->bp[i].off == off)
			return true;
	}
	return false;
}

bool debugmon_poll(debugmon_t *dm, lilpc_t *pc)
{
	if (dm->listen_fd < 0)
		return true; /* no monitor, always run */

	/* accept new connections */
	if (dm->client_fd < 0) {
		int fd = accept(dm->listen_fd, NULL, NULL);
		if (fd >= 0) {
			set_nonblock(fd);
			dm->client_fd = fd;
			dm->buf_len = 0;
			fprintf(stderr, "debugmon: client connected\n");
			dm_send(dm, "lilpc debug monitor\r\n");
			dm_send(dm, "CS:IP=%04X:%04X  (h for help)\r\n",
				pc->cpu.seg[SEG_CS].sel, pc->cpu.ip);
		}
	}

	if (dm->client_fd < 0)
		return !dm->paused;

	/* read available data */
	for (;;) {
		if (dm->buf_len >= (int)sizeof(dm->buf) - 1) {
			/* overflow, discard */
			dm->buf_len = 0;
		}

		ssize_t n = recv(dm->client_fd, dm->buf + dm->buf_len,
			sizeof(dm->buf) - 1 - dm->buf_len, 0);
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			/* error — disconnect */
			fprintf(stderr, "debugmon: client disconnected\n");
			close(dm->client_fd);
			dm->client_fd = -1;
			return !dm->paused;
		}
		if (n == 0) {
			/* EOF */
			fprintf(stderr, "debugmon: client disconnected\n");
			close(dm->client_fd);
			dm->client_fd = -1;
			return !dm->paused;
		}
		dm->buf_len += n;
		break;
	}

	/* process complete lines */
	dm->buf[dm->buf_len] = '\0';
	char *start = dm->buf;
	char *nl;
	while ((nl = strchr(start, '\n')) != NULL) {
		*nl = '\0';
		process_line(dm, pc, start);
		start = nl + 1;
	}

	/* shift remaining data to front */
	if (start != dm->buf) {
		int remain = dm->buf_len - (int)(start - dm->buf);
		if (remain > 0)
			memmove(dm->buf, start, remain);
		dm->buf_len = remain;
	}

	return !dm->paused;
}
