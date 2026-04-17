/*
 * ludica-launcher.c -- external process launcher + automation broker.
 *
 * Long-lived daemon that accepts bridge connections on TCP, spawns
 * games from an allowlist, captures stdout/stderr into circular log
 * buffers, and exposes a line-oriented text protocol for MCP bridges
 * and humans.  See doc/manual/ludica-mcp.md for the protocol.
 *
 * Phase 1 skeleton: .env loading, TCP accept, command dispatch for the
 * meta commands (ping, version, help, status).  Spawn/kill/log come in
 * later phases.
 */

#include "iox_loop.h"
#include "iox_fd.h"
#include "iox_signal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define LUDICA_LAUNCHER_VERSION "0.1.0"

#define LINE_MAX_BYTES   (64 * 1024)
#define DEFAULT_PORT     4000

/* ========== utility: fatal / warn ========== */

static void
die(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "ludica-launcher: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

static void
warn_(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "ludica-launcher: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* ========== dotenv parser (adapted from lumi sessdir_state.c) ========== */

/* unescape a double-quoted value in-place.
 * recognizes: \\ \" \$ \` */
static int
dotenv_unescape(char *buf)
{
	char *src = buf, *dst = buf;

	while (*src && *src != '"') {
		if (*src == '\\' && src[1]) {
			switch (src[1]) {
			case '\\': case '"': case '$': case '`':
				*dst++ = src[1];
				src += 2;
				continue;
			default:
				break;
			}
		}
		*dst++ = *src++;
	}
	*dst = '\0';
	return (int)(dst - buf);
}

typedef int (*dotenv_cb)(const char *key, const char *val, void *arg);

static int
dotenv_parse(const char *data, dotenv_cb cb, void *arg)
{
	const char *p = data;

	while (*p) {
		char key[256], val[4096];
		const char *eq, *eol;
		int klen, vlen;

		while (*p == '\n' || *p == '\r')
			p++;
		if (*p == '\0')
			break;
		if (*p == '#') {
			while (*p && *p != '\n')
				p++;
			continue;
		}

		eq = strchr(p, '=');
		eol = strchr(p, '\n');
		if (!eol)
			eol = p + strlen(p);
		if (!eq || eq > eol) {
			p = (*eol) ? eol + 1 : eol;
			continue;
		}

		klen = (int)(eq - p);
		if (klen <= 0 || klen >= (int)sizeof(key)) {
			p = (*eol) ? eol + 1 : eol;
			continue;
		}
		memcpy(key, p, (size_t)klen);
		key[klen] = '\0';

		eq++;
		if (*eq == '"') {
			const char *end;

			eq++;
			end = strchr(eq, '"');
			if (!end)
				end = eol;
			vlen = (int)(end - eq);
			if (vlen >= (int)sizeof(val))
				vlen = (int)sizeof(val) - 1;
			memcpy(val, eq, (size_t)vlen);
			val[vlen] = '\0';
			dotenv_unescape(val);
		} else {
			vlen = (int)(eol - eq);
			if (vlen >= (int)sizeof(val))
				vlen = (int)sizeof(val) - 1;
			memcpy(val, eq, (size_t)vlen);
			val[vlen] = '\0';
		}

		if (cb(key, val, arg) < 0)
			return -1;

		p = (*eol) ? eol + 1 : eol;
	}
	return 0;
}

static int
dotenv_setenv(const char *key, const char *val, void *arg)
{
	(void)arg;
	setenv(key, val, 1);
	return 0;
}

static void
load_env_file(const char *path)
{
	FILE *f;
	long sz;
	char *buf;
	size_t n;

	f = fopen(path, "r");
	if (!f)
		return; /* optional */

	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0 || sz > 1024 * 1024) {
		fclose(f);
		warn_("env file %s too large", path);
		return;
	}

	buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return;
	}
	n = fread(buf, 1, (size_t)sz, f);
	buf[n] = '\0';
	fclose(f);

	dotenv_parse(buf, dotenv_setenv, NULL);
	free(buf);
}

/* ========== per-connection session state ========== */

struct session {
	int fd;
	char linebuf[LINE_MAX_BYTES];
	int linelen;
	struct iox_loop *loop;
};

static void session_close(struct session *s);

static void
session_writef(struct session *s, const char *fmt, ...)
{
	char buf[8192];
	va_list ap;
	int n;
	ssize_t w;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n >= (int)sizeof(buf))
		n = (int)sizeof(buf) - 1;

	w = write(s->fd, buf, (size_t)n);
	(void)w;
}

/* ========== command dispatch ========== */

struct cmd_args {
	char *argv[32];
	int argc;
};

/* split a line in place on whitespace.  honors double-quoted segments
 * with backslash escapes.  modifies the input buffer. */
static void
parse_args(char *line, struct cmd_args *out)
{
	char *p = line, *dst;
	int n = 0;

	out->argc = 0;
	while (*p && n < (int)(sizeof(out->argv) / sizeof(out->argv[0]))) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0')
			break;

		if (*p == '"') {
			p++;
			dst = p;
			out->argv[n++] = dst;
			while (*p && *p != '"') {
				if (*p == '\\' && p[1]) {
					*dst++ = p[1];
					p += 2;
				} else {
					*dst++ = *p++;
				}
			}
			if (*p == '"')
				p++;
			*dst = '\0';
			if (*p)
				*p++ = '\0';
		} else {
			out->argv[n++] = p;
			while (*p && *p != ' ' && *p != '\t')
				p++;
			if (*p)
				*p++ = '\0';
		}
	}
	out->argc = n;
}

static void cmd_help(struct session *s, struct cmd_args *a);

static void
cmd_ping(struct session *s, struct cmd_args *a)
{
	(void)a;
	session_writef(s, "OK pong\n");
}

static void
cmd_version(struct session *s, struct cmd_args *a)
{
	(void)a;
	session_writef(s, "OK %s\n", LUDICA_LAUNCHER_VERSION);
}

static void
cmd_status(struct session *s, struct cmd_args *a)
{
	(void)a;
	session_writef(s, "OK never\n");
}

static void
cmd_session_info(struct session *s, struct cmd_args *a)
{
	(void)a;
	session_writef(s, "OK id=1 name=default attached=yes\n");
}

static void
cmd_session_list(struct session *s, struct cmd_args *a)
{
	(void)a;
	session_writef(s, "OK default\n");
}

struct command {
	const char *name;
	void (*fn)(struct session *s, struct cmd_args *a);
	const char *summary;
};

static const struct command commands[] = {
	{ "ping",         cmd_ping,         "health check; responds with pong" },
	{ "version",      cmd_version,      "report launcher version" },
	{ "help",         cmd_help,         "list commands or describe one" },
	{ "status",       cmd_status,       "report state of the spawned process" },
	{ "session_info", cmd_session_info, "report current session identity" },
	{ "session_list", cmd_session_list, "list all active sessions globally" },
	{ NULL, NULL, NULL }
};

static void
cmd_help(struct session *s, struct cmd_args *a)
{
	const struct command *c;

	if (a->argc >= 2) {
		for (c = commands; c->name; c++) {
			if (strcmp(c->name, a->argv[1]) == 0) {
				session_writef(s, "OK %s -- %s\n",
				    c->name, c->summary);
				return;
			}
		}
		session_writef(s, "ERR usage: no such command: %s\n",
		    a->argv[1]);
		return;
	}

	session_writef(s, "OK commands:\n");
	for (c = commands; c->name; c++)
		session_writef(s, " %-16s %s\n", c->name, c->summary);
}

static void
dispatch(struct session *s, char *line)
{
	struct cmd_args args;
	const struct command *c;

	parse_args(line, &args);
	if (args.argc == 0)
		return;

	for (c = commands; c->name; c++) {
		if (strcmp(c->name, args.argv[0]) == 0) {
			c->fn(s, &args);
			return;
		}
	}
	session_writef(s, "ERR usage: unknown command: %s\n", args.argv[0]);
}

/* ========== line-buffered input ========== */

static void
session_on_readable(struct iox_loop *loop, int fd, unsigned events, void *arg)
{
	struct session *s = arg;
	ssize_t n;
	int i;

	(void)loop;
	(void)events;

	n = read(fd, s->linebuf + s->linelen,
	    sizeof(s->linebuf) - (size_t)s->linelen - 1);
	if (n <= 0) {
		session_close(s);
		return;
	}
	s->linelen += (int)n;
	s->linebuf[s->linelen] = '\0';

	/* dispatch each complete line */
	for (;;) {
		char *nl = memchr(s->linebuf, '\n', (size_t)s->linelen);

		if (!nl) {
			if (s->linelen >= (int)sizeof(s->linebuf) - 1) {
				session_writef(s,
				    "ERR usage: line too long\n");
				s->linelen = 0;
			}
			break;
		}
		*nl = '\0';
		if (nl > s->linebuf && nl[-1] == '\r')
			nl[-1] = '\0';

		dispatch(s, s->linebuf);

		i = (int)(nl - s->linebuf) + 1;
		s->linelen -= i;
		if (s->linelen > 0)
			memmove(s->linebuf, s->linebuf + i,
			    (size_t)s->linelen);
		s->linebuf[s->linelen] = '\0';
	}
}

static struct session *
session_new(struct iox_loop *loop, int fd)
{
	struct session *s;

	s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->fd = fd;
	s->loop = loop;

	if (iox_fd_add(loop, fd, IOX_READ, session_on_readable, s) < 0) {
		free(s);
		return NULL;
	}
	return s;
}

static void
session_close(struct session *s)
{
	if (!s)
		return;
	iox_fd_remove(s->loop, s->fd);
	close(s->fd);
	free(s);
}

/* ========== listen socket ========== */

struct listener {
	int fd;
	struct iox_loop *loop;
};

static void
listener_on_readable(struct iox_loop *loop, int fd, unsigned events, void *arg)
{
	struct listener *l = arg;
	int cfd;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);

	(void)events;

	cfd = accept(fd, (struct sockaddr *)&addr, &alen);
	if (cfd < 0) {
		if (errno == EINTR || errno == EAGAIN)
			return;
		warn_("accept: %s", strerror(errno));
		return;
	}

	fcntl(cfd, F_SETFL, O_NONBLOCK);
	if (!session_new(loop, cfd)) {
		close(cfd);
		warn_("session_new failed");
	}
	(void)l;
}

static int
listen_bind(int port)
{
	int fd, one = 1;
	struct sockaddr_in addr;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((unsigned short)port);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	if (listen(fd, 8) < 0) {
		close(fd);
		return -1;
	}
	fcntl(fd, F_SETFL, O_NONBLOCK);
	return fd;
}

/* ========== signals ========== */

static void
on_sigint(struct iox_loop *loop, int signo, void *arg)
{
	(void)signo;
	(void)arg;
	iox_loop_stop(loop);
}

/* ========== main ========== */

static void
usage(void)
{
	fprintf(stderr,
	    "usage: ludica-launcher [--port=N] [--env-file=PATH]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	const char *env_file = ".env";
	int port = DEFAULT_PORT;
	int i;
	struct iox_loop *loop;
	struct listener lsn;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (strncmp(a, "--port=", 7) == 0) {
			port = atoi(a + 7);
		} else if (strncmp(a, "--env-file=", 11) == 0) {
			env_file = a + 11;
		} else if (strcmp(a, "--help") == 0 ||
		    strcmp(a, "-h") == 0) {
			usage();
		} else {
			fprintf(stderr, "ludica-launcher: unknown: %s\n", a);
			usage();
		}
	}

	load_env_file(env_file);

	/* env vars override defaults (LUDICA_MCP_PORT) */
	{
		const char *p = getenv("LUDICA_MCP_PORT");
		if (p && *p)
			port = atoi(p);
	}

	signal(SIGPIPE, SIG_IGN);

	loop = iox_loop_new();
	if (!loop)
		die("iox_loop_new failed");

	iox_signal_add(loop, SIGINT, on_sigint, NULL);
	iox_signal_add(loop, SIGTERM, on_sigint, NULL);

	lsn.fd = listen_bind(port);
	if (lsn.fd < 0)
		die("listen on :%d: %s", port, strerror(errno));
	lsn.loop = loop;

	if (iox_fd_add(loop, lsn.fd, IOX_READ,
	    listener_on_readable, &lsn) < 0)
		die("iox_fd_add listener");

	fprintf(stderr, "ludica-launcher %s listening on 127.0.0.1:%d\n",
	    LUDICA_LAUNCHER_VERSION, port);

	iox_loop_run(loop);

	close(lsn.fd);
	iox_loop_free(loop);
	return 0;
}
