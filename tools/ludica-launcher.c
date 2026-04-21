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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define LUDICA_LAUNCHER_VERSION "0.1.0"

#define LINE_MAX_BYTES     (64 * 1024)
#define DEFAULT_PORT       4000
#define DEFAULT_LOG_BYTES  (1024 * 1024)
#define STREAM_STDOUT      0
#define STREAM_STDERR      1
#define N_STREAMS          2

static size_t g_log_bytes = DEFAULT_LOG_BYTES;

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

/* ========== circular log buffer ==========
 *
 * One buffer per stream.  `total` tracks bytes ever written so callers
 * can detect wrap-around.  When the ring is full, oldest bytes are
 * silently overwritten.  Lines are not indexed; tail/head scan the ring
 * linearly. */

struct log_buf {
	char *data;
	size_t cap;      /* capacity (bytes) */
	size_t head;     /* write offset within ring */
	size_t len;      /* bytes currently stored (<= cap) */
	uint64_t total;  /* bytes ever written */
};

static int
log_buf_init(struct log_buf *b, size_t cap)
{
	b->data = malloc(cap);
	if (!b->data)
		return -1;
	b->cap = cap;
	b->head = b->len = 0;
	b->total = 0;
	return 0;
}

static void
log_buf_free(struct log_buf *b)
{
	free(b->data);
	b->data = NULL;
	b->cap = b->head = b->len = 0;
	b->total = 0;
}

static void
log_buf_clear(struct log_buf *b)
{
	b->head = b->len = 0;
	/* keep total; it's a monotonic byte counter */
}

static void
log_buf_write(struct log_buf *b, const char *src, size_t n)
{
	if (!b->data || b->cap == 0)
		return;
	b->total += n;
	if (n >= b->cap) {
		/* only the last `cap` bytes survive */
		memcpy(b->data, src + (n - b->cap), b->cap);
		b->head = 0;
		b->len = b->cap;
		return;
	}
	{
		size_t tail_space = b->cap - b->head;
		if (n <= tail_space) {
			memcpy(b->data + b->head, src, n);
			b->head += n;
			if (b->head == b->cap)
				b->head = 0;
		} else {
			memcpy(b->data + b->head, src, tail_space);
			memcpy(b->data, src + tail_space, n - tail_space);
			b->head = n - tail_space;
		}
		b->len += n;
		if (b->len > b->cap)
			b->len = b->cap;
	}
}

/* copy logical byte at index `i` (0 = oldest) */
static char
log_buf_at(const struct log_buf *b, size_t i)
{
	size_t start;

	if (b->len < b->cap)
		start = 0;
	else
		start = b->head;
	return b->data[(start + i) % b->cap];
}

/* write the last `n` lines (newline-terminated) into `out`.
 * Returns number of bytes written; truncates at out_cap. */
static size_t
log_buf_tail(const struct log_buf *b, int n, char *out, size_t out_cap)
{
	size_t count, i, newlines = 0, line_start;

	if (!b->data || b->len == 0 || n <= 0)
		return 0;

	/* Walk backwards from the end to find the start of the last n lines. */
	i = b->len;
	while (i > 0) {
		char c = log_buf_at(b, i - 1);
		i--;
		if (c == '\n') {
			newlines++;
			if (newlines > (size_t)n) {
				i++;
				break;
			}
		}
	}
	line_start = i;

	count = 0;
	for (i = line_start; i < b->len && count < out_cap; i++)
		out[count++] = log_buf_at(b, i);
	return count;
}

static size_t
log_buf_head(const struct log_buf *b, int n, char *out, size_t out_cap)
{
	size_t count = 0, i, newlines = 0;

	if (!b->data || b->len == 0 || n <= 0)
		return 0;

	for (i = 0; i < b->len && count < out_cap; i++) {
		char c = log_buf_at(b, i);
		out[count++] = c;
		if (c == '\n') {
			newlines++;
			if (newlines >= (size_t)n)
				break;
		}
	}
	return count;
}

/* Copy the logical ring contents into out (up to out_cap bytes).
 * Returns bytes copied. */
static size_t
log_buf_linearize(const struct log_buf *b, char *out, size_t out_cap)
{
	size_t n = b->len;
	size_t i;

	if (n > out_cap)
		n = out_cap;
	for (i = 0; i < n; i++)
		out[i] = log_buf_at(b, i);
	return n;
}

/* Emit lines numbered `first` through `last` (1-based, inclusive).
 * Clamps to available range; returns bytes written. */
static size_t
log_buf_range(const struct log_buf *b, int first, int last,
    char *out, size_t out_cap)
{
	size_t i, count = 0;
	int line = 1;
	int emit = 0;

	if (!b->data || b->len == 0 || first <= 0 || last < first)
		return 0;

	for (i = 0; i < b->len; i++) {
		char c = log_buf_at(b, i);
		emit = (line >= first && line <= last);
		if (emit && count < out_cap)
			out[count++] = c;
		if (c == '\n') {
			line++;
			if (line > last)
				break;
		}
	}
	return count;
}

/* Invoke cb for each line; stops when cb returns non-zero. */
typedef int (*line_cb)(const char *buf, size_t len, int line_no, void *arg);

static void
log_buf_for_each_line(const struct log_buf *b, line_cb cb, void *arg)
{
	char *tmp;
	size_t n, start = 0, i;
	int line = 1;

	if (!b->data || b->len == 0)
		return;
	tmp = malloc(b->len);
	if (!tmp)
		return;
	n = log_buf_linearize(b, tmp, b->len);
	for (i = 0; i < n; i++) {
		if (tmp[i] == '\n') {
			if (cb(tmp + start, i - start + 1, line, arg))
				goto done;
			line++;
			start = i + 1;
		}
	}
	if (start < n)
		cb(tmp + start, n - start, line, arg);
done:
	free(tmp);
}

/* ========== allowlist ==========
 *
 * LUDICA_MCP_ALLOWEXEC is a `:`-separated list of absolute paths.  A
 * spawn alias either matches the last path component of an entry, or
 * equals an entry verbatim. */

static int
allow_match(const char *alias, char *resolved, size_t resolved_cap)
{
	const char *env = getenv("LUDICA_MCP_ALLOWEXEC");
	const char *p, *start;

	if (!env || !*env)
		return 0;

	p = env;
	start = p;
	for (;;) {
		if (*p == ':' || *p == '\0') {
			size_t entry_len = (size_t)(p - start);
			if (entry_len > 0 && entry_len < resolved_cap) {
				const char *base = start;
				size_t base_len = entry_len;
				const char *slash;

				/* full path match */
				if (strlen(alias) == entry_len &&
				    memcmp(alias, start, entry_len) == 0) {
					memcpy(resolved, start, entry_len);
					resolved[entry_len] = '\0';
					return 1;
				}
				/* basename match */
				for (slash = start + entry_len - 1;
				    slash >= start; slash--) {
					if (*slash == '/') {
						base = slash + 1;
						base_len = (size_t)(start + entry_len - base);
						break;
					}
				}
				if (strlen(alias) == base_len &&
				    memcmp(alias, base, base_len) == 0) {
					memcpy(resolved, start, entry_len);
					resolved[entry_len] = '\0';
					return 1;
				}
			}
			if (*p == '\0')
				break;
			p++;
			start = p;
		} else {
			p++;
		}
	}
	return 0;
}

/* ========== per-connection session state ========== */

/* process state within a session */
enum proc_state {
	PROC_NEVER = 0,
	PROC_RUNNING,
	PROC_EXITED,
	PROC_SIGNALED,
};

struct session;

/* A game-session -- state for a spawned child and its captured logs.
 * Independent of any single TCP connection: a connection (struct session)
 * is attached to a gsession, and may detach (leaving the gsession alive
 * for later re-attach) or disconnect (which destroys the gsession unless
 * it was marked nokill). */
#define CTL_Q_CAP 16

struct gsession {
	int id;                 /* monotonically assigned, 1-based */
	char name[64];          /* "" if unnamed */
	int nokill;             /* survive owner disconnect */

	enum proc_state pstate;
	pid_t pid;
	int exit_code;
	int exit_signal;
	int out_fd;
	int err_fd;
	char bin_path[PATH_MAX];
	struct log_buf logs[N_STREAMS];

	/* control fd: socketpair to in-process automation sidecar
	 * (---controlfd=N passed to child).  Proxies the Control
	 * command subset; replies are FIFO-matched to the originating
	 * session via ctl_q. */
	int ctl_fd;
	char ctl_linebuf[8192];
	int ctl_linelen;
	struct session *ctl_q[CTL_Q_CAP];
	int ctl_q_head;
	int ctl_q_count;

	/* crash capture (Phase 5): populated on abnormal exit; cleared on
	 * next spawn.  core_path is the located core file; crash_summary is
	 * the one-line summary ("file:line in func: SIGNAME"), lazily
	 * computed on first query. */
	char core_path[PATH_MAX];
	char crash_summary[512];

	/* Phase 6 event line assembly: one accumulator per stream for
	 * EV_LOG_STDOUT_LINE / EV_LOG_STDERR_LINE emission.  Only grown
	 * while the owning session is subscribed. */
	char ev_line[N_STREAMS][2048];
	int ev_linelen[N_STREAMS];

	struct iox_loop *loop;
	struct session *owner;  /* currently attached connection, or NULL */
};

/* Phase 6 event flags (bitmask stored in session->sub_flags). */
#define EV_LOG_STDOUT_LINE (1u << 0)
#define EV_LOG_STDERR_LINE (1u << 1)
#define EV_PROCESS_EXIT    (1u << 2)
#define EV_CRASH           (1u << 3)
#define EV_CRASH_SUMMARY   (1u << 4)
#define EV_ALL             (EV_LOG_STDOUT_LINE | EV_LOG_STDERR_LINE | \
                            EV_PROCESS_EXIT | EV_CRASH | EV_CRASH_SUMMARY)

struct session {
	int fd;
	char linebuf[LINE_MAX_BYTES];
	int linelen;
	struct iox_loop *loop;
	struct gsession *g;     /* attached game-session (never NULL while open) */
	int closed;             /* request deferred close from inside dispatch */
	unsigned sub_flags;     /* EV_* mask: events this session wants */
	/* Body-framing scratch (see session_body_*).  A multi-line response
	 * is framed as `OK\n` <body> `END\n`; any body line starting with `\`
	 * or equal to `END` is escaped by prepending a single `\`. */
	char *body_line;
	size_t body_line_len;
	size_t body_line_cap;
};

static void session_close(struct session *s);
static struct gsession *gsession_new(struct iox_loop *loop);
static void gsession_destroy(struct gsession *g);

/* Active `log_jq` child subprocesses.  The list lets SIGCHLD locate jobs
 * by pid and lets session_close orphan them so stray async writes won't
 * touch freed sessions. */
struct log_jq_job {
	struct log_jq_job *next;
	struct session *s;      /* NULL if session closed (orphaned) */
	struct iox_loop *loop;
	pid_t pid;
	int stdin_fd;
	int stdout_fd;
	int stderr_fd;
	char *inbuf;
	size_t in_size;
	size_t in_off;
	char errbuf[512];
	size_t errlen;
	int reaped;
	int exit_status;
};
static struct log_jq_job *g_jq_jobs;

static void jq_job_finalize(struct log_jq_job *j);

/* Global gsession registry.  SIGCHLD scans this list to locate the
 * gsession owning a reaped pid; session_list and session_attach scan
 * it by name.  O(n) is fine for a handful of sessions. */
#define MAX_SESSIONS 64
static struct gsession *g_gsessions[MAX_SESSIONS];
static int g_next_gsession_id = 1;

static int
gsession_register(struct gsession *g)
{
	int i;

	for (i = 0; i < MAX_SESSIONS; i++) {
		if (!g_gsessions[i]) {
			g_gsessions[i] = g;
			g->id = g_next_gsession_id++;
			return 0;
		}
	}
	return -1;
}

static void
gsession_unregister(struct gsession *g)
{
	int i;

	for (i = 0; i < MAX_SESSIONS; i++) {
		if (g_gsessions[i] == g) {
			g_gsessions[i] = NULL;
			return;
		}
	}
}

static struct gsession *
gsession_by_pid(pid_t pid)
{
	int i;

	for (i = 0; i < MAX_SESSIONS; i++) {
		if (g_gsessions[i] && g_gsessions[i]->pid == pid &&
		    g_gsessions[i]->pstate == PROC_RUNNING)
			return g_gsessions[i];
	}
	return NULL;
}

static struct gsession *
gsession_by_name(const char *name)
{
	int i;

	if (!name || !*name)
		return NULL;
	for (i = 0; i < MAX_SESSIONS; i++) {
		if (g_gsessions[i] && strcmp(g_gsessions[i]->name, name) == 0)
			return g_gsessions[i];
	}
	return NULL;
}

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

/* ========== multi-line body framing ==========
 *
 * Protocol: a multi-line response is framed as an `OK\n` status line
 * followed by zero or more body lines, terminated by a sentinel line:
 *   - `END\n`             — success
 *   - `END ERR <reason>\n` — async failure (e.g. log_jq non-zero exit)
 * Body-line escape rule: if a body line starts with `\` (single
 * backslash) or with the three bytes `END`, one extra leading `\` is
 * prepended.  Decoders strip a single leading `\` from any body line.
 * `nc` users can find the end with `grep -n '^END\( \|$\)'` and
 * unescape with `sed 's/^\\\\/\\/'`.
 *
 * The session carries a small per-line buffer so arbitrary byte chunks
 * can be written across multiple calls: we accumulate until we see a
 * newline, then decide whether to prepend the escape.
 */
static void
body_flush_line(struct session *s, int has_nl)
{
	char *l = s->body_line;
	size_t n = s->body_line_len;
	int needs_esc = 0;
	ssize_t w;

	if (n > 0 && l[0] == '\\')
		needs_esc = 1;
	else if (n >= 3 && l[0] == 'E' && l[1] == 'N' && l[2] == 'D')
		needs_esc = 1;
	if (needs_esc) {
		w = write(s->fd, "\\", 1);
		(void)w;
	}
	if (n > 0) {
		w = write(s->fd, l, n);
		(void)w;
	}
	if (has_nl) {
		w = write(s->fd, "\n", 1);
		(void)w;
	}
	s->body_line_len = 0;
}

static void
session_body_begin(struct session *s)
{
	ssize_t w = write(s->fd, "OK\n", 3);
	(void)w;
	s->body_line_len = 0;
}

static void
session_body_write(struct session *s, const void *vbuf, size_t n)
{
	const char *buf = vbuf;
	size_t i;

	for (i = 0; i < n; i++) {
		if (buf[i] == '\n') {
			body_flush_line(s, 1);
			continue;
		}
		if (s->body_line_len + 1 > s->body_line_cap) {
			size_t ncap = s->body_line_cap ? s->body_line_cap * 2
			                               : 256;
			char *nb = realloc(s->body_line, ncap);
			if (!nb)
				return; /* drop overflow on OOM */
			s->body_line = nb;
			s->body_line_cap = ncap;
		}
		s->body_line[s->body_line_len++] = buf[i];
	}
}

static void
session_body_writef(struct session *s, const char *fmt, ...)
{
	char buf[4096];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n >= (int)sizeof(buf))
		n = (int)sizeof(buf) - 1;
	session_body_write(s, buf, (size_t)n);
}

static void
session_body_end(struct session *s)
{
	ssize_t w;
	if (s->body_line_len > 0) {
		/* Unterminated trailing fragment: flush with a synthesized
		 * newline so the END sentinel lands at column 0. */
		body_flush_line(s, 1);
	}
	w = write(s->fd, "END\n", 4);
	(void)w;
}

/* Terminate a multi-line response with an async-failure sentinel.  The
 * reason is sanitized to stay on a single line. */
static void
session_body_end_err(struct session *s, const char *fmt, ...)
{
	char buf[512], line[600];
	va_list ap;
	int n, m;
	size_t i;
	ssize_t w;

	if (s->body_line_len > 0)
		body_flush_line(s, 1);

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0) n = 0;
	if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
	for (i = 0; i < (size_t)n; i++) {
		if (buf[i] == '\n' || buf[i] == '\r')
			buf[i] = ' ';
	}
	m = snprintf(line, sizeof(line), "END ERR %.*s\n", n, buf);
	if (m < 0) return;
	if (m >= (int)sizeof(line)) m = (int)sizeof(line) - 1;
	w = write(s->fd, line, (size_t)m);
	(void)w;
}

/* ========== child process + log capture ========== */

static int compute_crash_summary(const char *binary, const char *core,
    int sig, char *out, size_t cap);

/* Feed bytes through the per-stream line accumulator, emitting an
 * EVENT log_{stdout,stderr}_line line for each complete '\n'-terminated
 * run when the owner is subscribed.  Silently truncates lines longer
 * than the accumulator. */
static void
ev_feed_stream(struct gsession *g, int stream_idx, const char *buf, size_t n)
{
	unsigned want = (stream_idx == STREAM_STDOUT)
	    ? EV_LOG_STDOUT_LINE : EV_LOG_STDERR_LINE;
	const char *tag = (stream_idx == STREAM_STDOUT)
	    ? "log_stdout_line" : "log_stderr_line";
	char *acc = g->ev_line[stream_idx];
	int *accn = &g->ev_linelen[stream_idx];
	size_t cap = sizeof(g->ev_line[stream_idx]);
	size_t i;

	if (!g->owner || !(g->owner->sub_flags & want)) {
		*accn = 0;
		return;
	}
	for (i = 0; i < n; i++) {
		char c = buf[i];
		if (c == '\n') {
			acc[*accn] = '\0';
			session_writef(g->owner, "EVENT %s %s\n", tag, acc);
			*accn = 0;
		} else if ((size_t)*accn + 1 < cap) {
			acc[(*accn)++] = c;
		}
	}
}

static void
pipe_on_readable(struct iox_loop *loop, int fd, unsigned events, void *arg)
{
	struct gsession *g = arg;
	int stream_idx = (fd == g->out_fd) ? STREAM_STDOUT : STREAM_STDERR;
	char buf[8192];
	ssize_t n;

	(void)loop;
	(void)events;

	n = read(fd, buf, sizeof(buf));
	if (n <= 0) {
		/* EOF or error -- stop watching this fd */
		iox_fd_remove(g->loop, fd);
		close(fd);
		if (fd == g->out_fd)
			g->out_fd = -1;
		else if (fd == g->err_fd)
			g->err_fd = -1;
		return;
	}
	log_buf_write(&g->logs[stream_idx], buf, (size_t)n);
	ev_feed_stream(g, stream_idx, buf, (size_t)n);
}

/* Drain any remaining readable bytes after child exits so we don't
 * lose the last few writes.  Non-blocking. */
static void
pipe_drain(struct gsession *g, int fd, int stream_idx)
{
	char buf[8192];
	ssize_t n;

	if (fd < 0)
		return;
	for (;;) {
		n = read(fd, buf, sizeof(buf));
		if (n <= 0)
			break;
		log_buf_write(&g->logs[stream_idx], buf, (size_t)n);
		ev_feed_stream(g, stream_idx, buf, (size_t)n);
	}
}

/* ---- control fd (proxy to in-process automation) ---- */

static int
ctl_q_push(struct gsession *g, struct session *s)
{
	int idx;

	if (g->ctl_q_count >= CTL_Q_CAP)
		return -1;
	idx = (g->ctl_q_head + g->ctl_q_count) % CTL_Q_CAP;
	g->ctl_q[idx] = s;
	g->ctl_q_count++;
	return 0;
}

static struct session *
ctl_q_pop(struct gsession *g)
{
	struct session *s;

	if (g->ctl_q_count == 0)
		return NULL;
	s = g->ctl_q[g->ctl_q_head];
	g->ctl_q[g->ctl_q_head] = NULL;
	g->ctl_q_head = (g->ctl_q_head + 1) % CTL_Q_CAP;
	g->ctl_q_count--;
	return s;
}

/* Null out any queued pending-reply entries referencing dead.
 * Called when a session is about to be freed or detaches to a
 * different gsession -- the reply will still arrive and be read,
 * but the pop will return NULL and we drop it silently. */
static void
ctl_q_scrub_session(struct session *dead)
{
	int i, j;

	for (i = 0; i < MAX_SESSIONS; i++) {
		struct gsession *g = g_gsessions[i];
		if (!g) continue;
		for (j = 0; j < CTL_Q_CAP; j++)
			if (g->ctl_q[j] == dead)
				g->ctl_q[j] = NULL;
	}
}

static void
ctl_close(struct gsession *g)
{
	struct session *s;

	if (g->ctl_fd < 0)
		return;
	while ((s = ctl_q_pop(g)) != NULL) {
		if (s) session_writef(s, "ERR control_closed\n");
	}
	iox_fd_remove(g->loop, g->ctl_fd);
	close(g->ctl_fd);
	g->ctl_fd = -1;
	g->ctl_linelen = 0;
}

static void
ctl_on_readable(struct iox_loop *loop, int fd, unsigned events, void *arg)
{
	struct gsession *g = arg;
	ssize_t n;

	(void)loop;
	(void)events;

	n = read(fd, g->ctl_linebuf + g->ctl_linelen,
	    sizeof(g->ctl_linebuf) - (size_t)g->ctl_linelen - 1);
	if (n <= 0) {
		ctl_close(g);
		return;
	}
	g->ctl_linelen += (int)n;
	g->ctl_linebuf[g->ctl_linelen] = '\0';

	for (;;) {
		char *nl = memchr(g->ctl_linebuf, '\n',
		    (size_t)g->ctl_linelen);
		struct session *s;
		int consumed;

		if (!nl) {
			if (g->ctl_linelen >= (int)sizeof(g->ctl_linebuf) - 1)
				g->ctl_linelen = 0;
			break;
		}
		*nl = '\0';
		if (nl > g->ctl_linebuf && nl[-1] == '\r')
			nl[-1] = '\0';

		s = ctl_q_pop(g);
		if (s)
			session_writef(s, "%s\n", g->ctl_linebuf);

		consumed = (int)(nl - g->ctl_linebuf) + 1;
		g->ctl_linelen -= consumed;
		if (g->ctl_linelen > 0)
			memmove(g->ctl_linebuf, g->ctl_linebuf + consumed,
			    (size_t)g->ctl_linelen);
		g->ctl_linebuf[g->ctl_linelen] = '\0';
	}
}

/* Called when the gsession tears down and the process is still recorded
 * as running -- send SIGKILL, reap, don't leave zombies. */
static void
proc_reap_if_running(struct gsession *g)
{
	int status;
	pid_t r;

	if (g->pstate != PROC_RUNNING || g->pid <= 0)
		return;
	kill(g->pid, SIGKILL);
	r = waitpid(g->pid, &status, 0);
	if (r == g->pid) {
		if (WIFEXITED(status)) {
			g->pstate = PROC_EXITED;
			g->exit_code = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			g->pstate = PROC_SIGNALED;
			g->exit_signal = WTERMSIG(status);
		}
	}
}

static int core_locate_for(const char *binary, pid_t pid,
    char *out, size_t cap);

/* SIGCHLD self-pipe handler: reap any children that exited, update
 * the owning gsession's process state.  Log fds remain open so the
 * drain callback can still flush remaining bytes. */
static void
on_sigchld(struct iox_loop *loop, int signo, void *arg)
{
	int status;
	pid_t pid;
	struct gsession *g;

	(void)loop;
	(void)signo;
	(void)arg;

	for (;;) {
		struct log_jq_job *j;
		pid = waitpid(-1, &status, WNOHANG);
		if (pid <= 0)
			break;
		for (j = g_jq_jobs; j; j = j->next) {
			if (j->pid == pid) {
				j->reaped = 1;
				j->exit_status = status;
				jq_job_finalize(j);
				break;
			}
		}
		g = gsession_by_pid(pid);
		if (!g)
			continue;
		if (WIFEXITED(status)) {
			g->pstate = PROC_EXITED;
			g->exit_code = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			char path[PATH_MAX];
			g->pstate = PROC_SIGNALED;
			g->exit_signal = WTERMSIG(status);
			/* Attempt auto-capture: try to locate a core file now
			 * so `gdb_core_find` returns it immediately and the
			 * coredump is recorded even if the session forgets to
			 * query.  Failure is non-fatal and silent. */
			if (g->bin_path[0] &&
			    core_locate_for(g->bin_path, pid, path,
			        sizeof(path)) == 0) {
				snprintf(g->core_path, sizeof(g->core_path),
				    "%s", path);
			}
		}
		pipe_drain(g, g->out_fd, STREAM_STDOUT);
		pipe_drain(g, g->err_fd, STREAM_STDERR);

		/* Phase 6: emit lifecycle events to a subscribed owner. */
		if (g->owner) {
			if (WIFEXITED(status) &&
			    (g->owner->sub_flags & EV_PROCESS_EXIT)) {
				session_writef(g->owner,
				    "EVENT process_exit pid=%d code=%d\n",
				    (int)pid, g->exit_code);
			}
			if (WIFSIGNALED(status)) {
				if (g->owner->sub_flags & EV_CRASH) {
					session_writef(g->owner,
					    "EVENT crash pid=%d sig=%d core=%s\n",
					    (int)pid, g->exit_signal,
					    g->core_path[0] ? g->core_path : "none");
				}
				if ((g->owner->sub_flags & EV_CRASH_SUMMARY) &&
				    g->core_path[0] && g->bin_path[0]) {
					char summary[512];
					if (compute_crash_summary(g->bin_path,
					        g->core_path, g->exit_signal,
					        summary, sizeof(summary)) == 0) {
						snprintf(g->crash_summary,
						    sizeof(g->crash_summary),
						    "%s", summary);
						session_writef(g->owner,
						    "EVENT crash_summary %s\n",
						    summary);
					}
				}
			}
		}
	}
}

/* Fork/exec a child with stdout/stderr piped back to us.
 * argv[0] will be the resolved binary path.  Returns 0 on success. */
static int
proc_spawn(struct gsession *g, const char *bin, char *const argv[],
    char *err_out, size_t err_cap)
{
	int outp[2], errp[2];
	int ctlp[2];
	pid_t pid;
	char ctl_arg[32];
	char *final_argv[34];
	int n_argv = 0;
	int i;

	if (g->pstate == PROC_RUNNING) {
		snprintf(err_out, err_cap, "process already running");
		return -1;
	}

	if (pipe(outp) < 0) {
		snprintf(err_out, err_cap, "pipe: %s", strerror(errno));
		return -1;
	}
	if (pipe(errp) < 0) {
		snprintf(err_out, err_cap, "pipe: %s", strerror(errno));
		close(outp[0]); close(outp[1]);
		return -1;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, ctlp) < 0) {
		ctlp[0] = ctlp[1] = -1;
	}

	/* build argv with ---controlfd=N appended */
	for (i = 0; argv[i] && n_argv < 32; i++)
		final_argv[n_argv++] = argv[i];
	if (ctlp[1] >= 0 && n_argv < 32) {
		snprintf(ctl_arg, sizeof(ctl_arg), "---controlfd=%d", ctlp[1]);
		final_argv[n_argv++] = ctl_arg;
	}
	final_argv[n_argv] = NULL;

	pid = fork();
	if (pid < 0) {
		snprintf(err_out, err_cap, "fork: %s", strerror(errno));
		close(outp[0]); close(outp[1]);
		close(errp[0]); close(errp[1]);
		if (ctlp[0] >= 0) close(ctlp[0]);
		if (ctlp[1] >= 0) close(ctlp[1]);
		return -1;
	}
	if (pid == 0) {
		/* child */
		dup2(outp[1], STDOUT_FILENO);
		dup2(errp[1], STDERR_FILENO);
		close(outp[0]); close(outp[1]);
		close(errp[0]); close(errp[1]);
		if (ctlp[0] >= 0) close(ctlp[0]);
		/* ctlp[1] survives exec; its number is in argv */
		/* reset SIGPIPE so child sees default behavior */
		signal(SIGPIPE, SIG_DFL);
		execv(bin, final_argv);
		fprintf(stderr, "exec %s: %s\n", bin, strerror(errno));
		_exit(127);
	}

	/* parent */
	close(outp[1]);
	close(errp[1]);
	if (ctlp[1] >= 0) close(ctlp[1]);
	fcntl(outp[0], F_SETFL, O_NONBLOCK);
	fcntl(errp[0], F_SETFL, O_NONBLOCK);
	if (ctlp[0] >= 0) {
		fcntl(ctlp[0], F_SETFL, O_NONBLOCK);
		fcntl(ctlp[0], F_SETFD, FD_CLOEXEC);
	}

	/* previous process state carries log buffers; clear them per design */
	for (i = 0; i < N_STREAMS; i++)
		log_buf_clear(&g->logs[i]);

	/* clear any crash info from the previous run */
	g->core_path[0] = '\0';
	g->crash_summary[0] = '\0';

	g->pid = pid;
	g->pstate = PROC_RUNNING;
	g->exit_code = 0;
	g->exit_signal = 0;
	g->out_fd = outp[0];
	g->err_fd = errp[0];
	g->ctl_fd = ctlp[0];
	g->ctl_linelen = 0;
	g->ctl_q_head = 0;
	g->ctl_q_count = 0;
	memset(g->ctl_q, 0, sizeof(g->ctl_q));
	snprintf(g->bin_path, sizeof(g->bin_path), "%s", bin);

	iox_fd_add(g->loop, g->out_fd, IOX_READ, pipe_on_readable, g);
	iox_fd_add(g->loop, g->err_fd, IOX_READ, pipe_on_readable, g);
	if (g->ctl_fd >= 0)
		iox_fd_add(g->loop, g->ctl_fd, IOX_READ, ctl_on_readable, g);
	return 0;
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
	switch (s->g->pstate) {
	case PROC_NEVER:
		session_writef(s, "OK never\n");
		break;
	case PROC_RUNNING:
		session_writef(s, "OK running pid=%d\n", (int)s->g->pid);
		break;
	case PROC_EXITED:
		session_writef(s, "OK exited code=%d\n", s->g->exit_code);
		break;
	case PROC_SIGNALED:
		session_writef(s, "OK signaled sig=%d\n", s->g->exit_signal);
		break;
	}
}

static void
cmd_spawn(struct session *s, struct cmd_args *a)
{
	char resolved[PATH_MAX];
	char err[256];
	char *child_argv[32];
	int n_argv = 0;
	int i;

	if (a->argc < 2) {
		session_writef(s, "ERR usage: spawn <alias> [args...]\n");
		return;
	}
	if (!allow_match(a->argv[1], resolved, sizeof(resolved))) {
		session_writef(s,
		    "ERR not_allowed: %s not in LUDICA_MCP_ALLOWEXEC\n",
		    a->argv[1]);
		return;
	}

	/* kill any existing process first (design: second spawn replaces) */
	if (s->g->pstate == PROC_RUNNING) {
		proc_reap_if_running(s->g);
		if (s->g->out_fd >= 0) {
			iox_fd_remove(s->g->loop, s->g->out_fd);
			close(s->g->out_fd);
			s->g->out_fd = -1;
		}
		if (s->g->err_fd >= 0) {
			iox_fd_remove(s->g->loop, s->g->err_fd);
			close(s->g->err_fd);
			s->g->err_fd = -1;
		}
		ctl_close(s->g);
	}

	/* build argv: resolved path, then user args after alias */
	child_argv[n_argv++] = resolved;
	for (i = 2; i < a->argc &&
	    n_argv < (int)(sizeof(child_argv) / sizeof(child_argv[0])) - 1;
	    i++) {
		child_argv[n_argv++] = a->argv[i];
	}
	child_argv[n_argv] = NULL;

	if (proc_spawn(s->g, resolved, child_argv, err, sizeof(err)) < 0) {
		session_writef(s, "ERR exec: %s\n", err);
		return;
	}
	session_writef(s, "OK pid=%d\n", (int)s->g->pid);
}

static void
cmd_kill(struct session *s, struct cmd_args *a)
{
	int sig = SIGTERM;

	if (s->g->pstate != PROC_RUNNING) {
		session_writef(s, "ERR no_process\n");
		return;
	}
	if (a->argc >= 2) {
		sig = atoi(a->argv[1]);
		if (sig <= 0) {
			session_writef(s,
			    "ERR usage: kill [signal]\n");
			return;
		}
	}
	if (kill(s->g->pid, sig) < 0) {
		session_writef(s, "ERR internal: kill: %s\n",
		    strerror(errno));
		return;
	}
	session_writef(s, "OK killed pid=%d\n", (int)s->g->pid);
}

/* Parse stream args from argv[start..].  Returns a mask of selected
 * streams; if none specified, returns all. */
static unsigned
parse_streams(struct cmd_args *a, int start)
{
	unsigned mask = 0;
	int i;

	for (i = start; i < a->argc; i++) {
		if (a->argv[i][0] == '-' && a->argv[i][1] == '-')
			continue; /* skip --flags */
		if (strcmp(a->argv[i], "stdout") == 0)
			mask |= (1u << STREAM_STDOUT);
		else if (strcmp(a->argv[i], "stderr") == 0)
			mask |= (1u << STREAM_STDERR);
	}
	if (mask == 0)
		mask = (1u << STREAM_STDOUT) | (1u << STREAM_STDERR);
	return mask;
}

static int
parse_positive_int(const char *s, int *out)
{
	char *end;
	long v;

	if (!s || !*s)
		return -1;
	v = strtol(s, &end, 10);
	if (*end != '\0' || v <= 0 || v > INT_MAX)
		return -1;
	*out = (int)v;
	return 0;
}

static void
write_log_chunk(struct session *s, const char *stream_label,
    const char *data, size_t n)
{
	/* prefix each line with stream tag when emitting from both streams;
	 * when only one stream selected we skip the prefix for cleaner
	 * output.  Routed through session_body_write so lines are escaped
	 * per the multi-line framing rule. */
	if (stream_label) {
		char prefix[32];
		size_t plen;
		size_t i = 0, line_start = 0;

		plen = (size_t)snprintf(prefix, sizeof(prefix), "%s: ",
		    stream_label);
		while (i < n) {
			if (data[i] == '\n') {
				session_body_write(s, prefix, plen);
				session_body_write(s, data + line_start,
				    i - line_start + 1);
				line_start = i + 1;
			}
			i++;
		}
		if (line_start < n) {
			session_body_write(s, prefix, plen);
			session_body_write(s, data + line_start,
			    n - line_start);
		}
	} else {
		session_body_write(s, data, n);
	}
}

static void
cmd_log_tail(struct session *s, struct cmd_args *a)
{
	int n;
	unsigned mask;
	char *out;
	size_t written;
	int streams_selected;

	if (a->argc < 2 || parse_positive_int(a->argv[1], &n) < 0) {
		session_writef(s, "ERR usage: log_tail <n> [streams...]\n");
		return;
	}
	mask = parse_streams(a, 2);
	streams_selected =
	    !!(mask & (1u << STREAM_STDOUT)) +
	    !!(mask & (1u << STREAM_STDERR));

	out = malloc(g_log_bytes + 16);
	if (!out) {
		session_writef(s, "ERR internal: out of memory\n");
		return;
	}
	session_body_begin(s);
	if (mask & (1u << STREAM_STDOUT)) {
		written = log_buf_tail(&s->g->logs[STREAM_STDOUT], n, out,
		    g_log_bytes);
		write_log_chunk(s, streams_selected > 1 ? "stdout" : NULL,
		    out, written);
	}
	if (mask & (1u << STREAM_STDERR)) {
		written = log_buf_tail(&s->g->logs[STREAM_STDERR], n, out,
		    g_log_bytes);
		write_log_chunk(s, streams_selected > 1 ? "stderr" : NULL,
		    out, written);
	}
	session_body_end(s);
	free(out);
}

static void
cmd_log_head(struct session *s, struct cmd_args *a)
{
	int n;
	unsigned mask;
	char *out;
	size_t written;
	int streams_selected;

	if (a->argc < 2 || parse_positive_int(a->argv[1], &n) < 0) {
		session_writef(s, "ERR usage: log_head <n> [streams...]\n");
		return;
	}
	mask = parse_streams(a, 2);
	streams_selected =
	    !!(mask & (1u << STREAM_STDOUT)) +
	    !!(mask & (1u << STREAM_STDERR));

	out = malloc(g_log_bytes + 16);
	if (!out) {
		session_writef(s, "ERR internal: out of memory\n");
		return;
	}
	session_body_begin(s);
	if (mask & (1u << STREAM_STDOUT)) {
		written = log_buf_head(&s->g->logs[STREAM_STDOUT], n, out,
		    g_log_bytes);
		write_log_chunk(s, streams_selected > 1 ? "stdout" : NULL,
		    out, written);
	}
	if (mask & (1u << STREAM_STDERR)) {
		written = log_buf_head(&s->g->logs[STREAM_STDERR], n, out,
		    g_log_bytes);
		write_log_chunk(s, streams_selected > 1 ? "stderr" : NULL,
		    out, written);
	}
	session_body_end(s);
	free(out);
}

static void
cmd_log_range(struct session *s, struct cmd_args *a)
{
	int first, last;
	unsigned mask;
	char *out;
	size_t written;
	int streams_selected;

	if (a->argc < 3 ||
	    parse_positive_int(a->argv[1], &first) < 0 ||
	    parse_positive_int(a->argv[2], &last) < 0 ||
	    last < first) {
		session_writef(s,
		    "ERR usage: log_range <a> <b> [streams...]\n");
		return;
	}
	mask = parse_streams(a, 3);
	streams_selected =
	    !!(mask & (1u << STREAM_STDOUT)) +
	    !!(mask & (1u << STREAM_STDERR));

	out = malloc(g_log_bytes + 16);
	if (!out) {
		session_writef(s, "ERR internal: out of memory\n");
		return;
	}
	session_body_begin(s);
	if (mask & (1u << STREAM_STDOUT)) {
		written = log_buf_range(&s->g->logs[STREAM_STDOUT], first, last,
		    out, g_log_bytes);
		write_log_chunk(s, streams_selected > 1 ? "stdout" : NULL,
		    out, written);
	}
	if (mask & (1u << STREAM_STDERR)) {
		written = log_buf_range(&s->g->logs[STREAM_STDERR], first, last,
		    out, g_log_bytes);
		write_log_chunk(s, streams_selected > 1 ? "stderr" : NULL,
		    out, written);
	}
	session_body_end(s);
	free(out);
}

struct grep_ctx {
	struct session *s;
	regex_t *re;
	int ctx_before;
	int ctx_after;
	const char *stream_label;
	/* rolling window of recent lines for -B (before-context) */
	char **hist;
	size_t *hist_lens;
	int *hist_lineno;
	int hist_cap;
	int hist_count;  /* number of valid entries in the ring */
	int hist_head;   /* next write position */
	int after_left;  /* remaining lines to emit after a match */
	int last_emitted_line;
};

static void
grep_emit_line(struct grep_ctx *g, int line_no, const char *buf, size_t len)
{
	char header[64];
	int n;

	if (g->last_emitted_line > 0 && line_no > g->last_emitted_line + 1)
		session_body_write(g->s, "--\n", 3);
	if (g->stream_label)
		n = snprintf(header, sizeof(header), "%s:%d: ",
		    g->stream_label, line_no);
	else
		n = snprintf(header, sizeof(header), "%d: ", line_no);
	session_body_write(g->s, header, (size_t)n);
	session_body_write(g->s, buf, len);
	if (len == 0 || buf[len - 1] != '\n')
		session_body_write(g->s, "\n", 1);
	g->last_emitted_line = line_no;
}

static int
grep_each_line(const char *buf, size_t len, int line_no, void *arg)
{
	struct grep_ctx *g = arg;
	char *nul;
	int is_match;
	int i;

	/* regexec needs NUL-terminated input; use a temp copy. */
	nul = malloc(len + 1);
	if (!nul)
		return 0;
	memcpy(nul, buf, len);
	if (len > 0 && nul[len - 1] == '\n')
		nul[len - 1] = '\0';
	else
		nul[len] = '\0';
	is_match = regexec(g->re, nul, 0, NULL, 0) == 0;
	free(nul);

	if (is_match) {
		/* flush before-context */
		if (g->ctx_before > 0 && g->hist_count > 0) {
			int to_emit = g->hist_count;
			int start;

			if (to_emit > g->ctx_before)
				to_emit = g->ctx_before;
			start = (g->hist_head - to_emit + g->hist_cap) %
			    g->hist_cap;
			for (i = 0; i < to_emit; i++) {
				int idx = (start + i) % g->hist_cap;
				grep_emit_line(g, g->hist_lineno[idx],
				    g->hist[idx], g->hist_lens[idx]);
			}
		}
		grep_emit_line(g, line_no, buf, len);
		g->after_left = g->ctx_after;
	} else if (g->after_left > 0) {
		grep_emit_line(g, line_no, buf, len);
		g->after_left--;
	}

	/* push into history ring (for next match's before-context) */
	if (g->ctx_before > 0 && g->hist_cap > 0) {
		int h = g->hist_head;
		free(g->hist[h]);
		g->hist[h] = malloc(len);
		if (g->hist[h]) {
			memcpy(g->hist[h], buf, len);
			g->hist_lens[h] = len;
			g->hist_lineno[h] = line_no;
		} else {
			g->hist_lens[h] = 0;
			g->hist_lineno[h] = 0;
		}
		g->hist_head = (h + 1) % g->hist_cap;
		if (g->hist_count < g->hist_cap)
			g->hist_count++;
	}
	return 0;
}

static void
cmd_log_grep(struct session *s, struct cmd_args *a)
{
	unsigned mask;
	int streams_selected;
	int ctx = 0;
	const char *pattern = NULL;
	regex_t re;
	int re_ok = 0;
	struct grep_ctx gctx;
	int i;

	for (i = 1; i < a->argc; i++) {
		if (strncmp(a->argv[i], "--ctx=", 6) == 0) {
			ctx = atoi(a->argv[i] + 6);
			if (ctx < 0)
				ctx = 0;
		} else if (a->argv[i][0] != '-' && !pattern) {
			pattern = a->argv[i];
		}
	}
	if (!pattern) {
		session_writef(s,
		    "ERR usage: log_grep <regex> [--ctx=<n>] [streams...]\n");
		return;
	}

	if (regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
		session_writef(s, "ERR usage: bad regex: %s\n", pattern);
		return;
	}
	re_ok = 1;

	mask = parse_streams(a, 1);
	streams_selected =
	    !!(mask & (1u << STREAM_STDOUT)) +
	    !!(mask & (1u << STREAM_STDERR));

	memset(&gctx, 0, sizeof(gctx));
	gctx.s = s;
	gctx.re = &re;
	gctx.ctx_before = ctx;
	gctx.ctx_after = ctx;
	if (ctx > 0) {
		gctx.hist_cap = ctx;
		gctx.hist = calloc((size_t)ctx, sizeof(char *));
		gctx.hist_lens = calloc((size_t)ctx, sizeof(size_t));
		gctx.hist_lineno = calloc((size_t)ctx, sizeof(int));
		if (!gctx.hist || !gctx.hist_lens || !gctx.hist_lineno) {
			session_writef(s, "ERR internal: out of memory\n");
			goto cleanup;
		}
	}

	session_body_begin(s);

	if (mask & (1u << STREAM_STDOUT)) {
		gctx.stream_label = streams_selected > 1 ? "stdout" : NULL;
		gctx.hist_count = gctx.hist_head = 0;
		gctx.after_left = 0;
		gctx.last_emitted_line = 0;
		log_buf_for_each_line(&s->g->logs[STREAM_STDOUT],
		    grep_each_line, &gctx);
	}
	if (mask & (1u << STREAM_STDERR)) {
		gctx.stream_label = streams_selected > 1 ? "stderr" : NULL;
		gctx.hist_count = gctx.hist_head = 0;
		gctx.after_left = 0;
		gctx.last_emitted_line = 0;
		log_buf_for_each_line(&s->g->logs[STREAM_STDERR],
		    grep_each_line, &gctx);
	}

	session_body_end(s);

cleanup:
	if (re_ok)
		regfree(&re);
	if (gctx.hist) {
		for (i = 0; i < gctx.hist_cap; i++)
			free(gctx.hist[i]);
		free(gctx.hist);
	}
	free(gctx.hist_lens);
	free(gctx.hist_lineno);
}

/*
 * log_where: structural filter over JSON log lines.
 *
 * Each argument of the form KEY=VAL or KEY~REGEX is a predicate.  A line
 * matches when it parses as a flat JSON object AND every predicate holds.
 * Non-JSON lines and lines missing a predicate's key are skipped.
 */

/* Skip ASCII whitespace.  Advances *p while *p < end && isspace. */
static void
jw_skip_ws(const char **p, const char *end)
{
	while (*p < end) {
		char c = **p;
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			(*p)++;
		else
			break;
	}
}

/* Advance past a JSON string starting at *p (points at opening '"').
 * On success, leaves *p just past the closing '"' and returns 0. */
static int
jw_skip_string(const char **p, const char *end)
{
	if (*p >= end || **p != '"')
		return -1;
	(*p)++;
	while (*p < end) {
		char c = **p;
		if (c == '\\') {
			if (*p + 1 >= end) return -1;
			*p += 2;
			continue;
		}
		if (c == '"') { (*p)++; return 0; }
		(*p)++;
	}
	return -1;
}

/* Compare a quoted JSON string at *p (points at '"') to a C string `want`,
 * honoring simple backslash escapes ("\\\"", "\\\\", "\\n", "\\t", "\\r",
 * "\\b", "\\f", and "\\uXXXX" for code points < 0x80).  Advances *p past
 * the closing quote on success.  Returns 1 on match, 0 on no-match. */
static int
jw_string_eq(const char **p, const char *end, const char *want)
{
	const char *q = *p;
	if (q >= end || *q != '"') return 0;
	q++;
	const char *w = want;
	while (q < end && *q != '"') {
		char out;
		if (*q == '\\') {
			if (q + 1 >= end) return 0;
			switch (q[1]) {
			case '"':  out = '"';  q += 2; break;
			case '\\': out = '\\'; q += 2; break;
			case '/':  out = '/';  q += 2; break;
			case 'n':  out = '\n'; q += 2; break;
			case 'r':  out = '\r'; q += 2; break;
			case 't':  out = '\t'; q += 2; break;
			case 'b':  out = '\b'; q += 2; break;
			case 'f':  out = '\f'; q += 2; break;
			case 'u': {
				if (q + 6 > end) return 0;
				unsigned v = 0;
				for (int i = 0; i < 4; i++) {
					char c = q[2 + i];
					v <<= 4;
					if (c >= '0' && c <= '9') v |= c - '0';
					else if (c >= 'a' && c <= 'f') v |= 10 + c - 'a';
					else if (c >= 'A' && c <= 'F') v |= 10 + c - 'A';
					else return 0;
				}
				if (v >= 0x80) return 0; /* unsupported; fail compare */
				out = (char)v;
				q += 6;
				break;
			}
			default: return 0;
			}
		} else {
			out = *q++;
		}
		if (*w++ != out) {
			/* consume the rest of the string and bail */
			while (q < end && *q != '"') {
				if (*q == '\\' && q + 1 < end) q += 2;
				else q++;
			}
			if (q < end) q++;
			*p = q;
			return 0;
		}
	}
	if (q >= end || *q != '"') return 0;
	q++;
	*p = q;
	return *w == '\0';
}

/* Decode the quoted JSON string at *p into out (NUL-terminated, truncated to
 * cap-1 bytes).  Advances *p past closing quote.  Returns 0 on success. */
static int
jw_string_decode(const char **p, const char *end, char *out, size_t cap)
{
	const char *q = *p;
	size_t n = 0;
	if (q >= end || *q != '"') return -1;
	q++;
	while (q < end && *q != '"') {
		char c;
		if (*q == '\\') {
			if (q + 1 >= end) return -1;
			switch (q[1]) {
			case '"':  c = '"';  q += 2; break;
			case '\\': c = '\\'; q += 2; break;
			case '/':  c = '/';  q += 2; break;
			case 'n':  c = '\n'; q += 2; break;
			case 'r':  c = '\r'; q += 2; break;
			case 't':  c = '\t'; q += 2; break;
			case 'b':  c = '\b'; q += 2; break;
			case 'f':  c = '\f'; q += 2; break;
			case 'u': {
				if (q + 6 > end) return -1;
				unsigned v = 0;
				for (int i = 0; i < 4; i++) {
					char ch = q[2 + i];
					v <<= 4;
					if (ch >= '0' && ch <= '9') v |= ch - '0';
					else if (ch >= 'a' && ch <= 'f') v |= 10 + ch - 'a';
					else if (ch >= 'A' && ch <= 'F') v |= 10 + ch - 'A';
					else return -1;
				}
				q += 6;
				if (v < 0x80) c = (char)v;
				else c = '?'; /* punt high codepoints */
				break;
			}
			default: return -1;
			}
		} else {
			c = *q++;
		}
		if (n + 1 < cap) out[n++] = c;
	}
	if (q >= end || *q != '"') return -1;
	q++;
	if (n < cap) out[n] = '\0';
	else if (cap > 0) out[cap - 1] = '\0';
	*p = q;
	return 0;
}

/* Locate the value for `key` in a flat JSON object.  On success writes an
 * unescaped-string form of the value into `out` (for strings), or the raw
 * token text (for numbers/true/false/null).  Returns 1 on found, 0 if key
 * absent, -1 if buf is not a JSON object. */
static int
jw_get(const char *buf, size_t len, const char *key, char *out, size_t cap)
{
	const char *p = buf, *end = buf + len;
	jw_skip_ws(&p, end);
	if (p >= end || *p != '{') return -1;
	p++;
	for (;;) {
		jw_skip_ws(&p, end);
		if (p >= end) return -1;
		if (*p == '}') return 0;
		if (*p != '"') return -1;
		int matched = jw_string_eq(&p, end, key);
		if (!matched && (p == buf || p > end)) return -1;
		jw_skip_ws(&p, end);
		if (p >= end || *p != ':') return -1;
		p++;
		jw_skip_ws(&p, end);
		if (p >= end) return -1;
		const char *vbeg = p, *vend;
		if (*p == '"') {
			if (matched) {
				if (jw_string_decode(&p, end, out, cap) < 0)
					return -1;
				return 1;
			}
			if (jw_skip_string(&p, end) < 0) return -1;
		} else {
			/* scan to , or } or ws at depth 0 */
			while (p < end && *p != ',' && *p != '}'
			    && *p != ' ' && *p != '\t'
			    && *p != '\n' && *p != '\r')
				p++;
			vend = p;
			if (matched) {
				size_t n = (size_t)(vend - vbeg);
				if (n >= cap) n = cap - 1;
				memcpy(out, vbeg, n);
				out[n] = '\0';
				return 1;
			}
		}
		jw_skip_ws(&p, end);
		if (p >= end) return -1;
		if (*p == ',') { p++; continue; }
		if (*p == '}') return 0;
		return -1;
	}
}

struct where_pred {
	const char *key;
	int op;             /* '=' or '~' */
	const char *val;    /* eq text */
	regex_t re;
	int re_ok;
};

struct where_ctx {
	struct session *s;
	struct where_pred *preds;
	int npred;
	const char *stream_label;
	int last_emitted_line;
};

static void
where_emit_line(struct where_ctx *g, int line_no, const char *buf, size_t len)
{
	char header[64];
	int n;

	if (g->last_emitted_line > 0 && line_no > g->last_emitted_line + 1)
		session_body_write(g->s, "--\n", 3);
	if (g->stream_label)
		n = snprintf(header, sizeof(header), "%s:%d: ",
		    g->stream_label, line_no);
	else
		n = snprintf(header, sizeof(header), "%d: ", line_no);
	session_body_write(g->s, header, (size_t)n);
	session_body_write(g->s, buf, len);
	if (len == 0 || buf[len - 1] != '\n')
		session_body_write(g->s, "\n", 1);
	g->last_emitted_line = line_no;
}

static int
where_each_line(const char *buf, size_t len, int line_no, void *arg)
{
	struct where_ctx *g = arg;
	char val[512];
	int i;

	/* strip trailing newline for scanning */
	size_t scan_len = len;
	if (scan_len > 0 && buf[scan_len - 1] == '\n') scan_len--;

	for (i = 0; i < g->npred; i++) {
		struct where_pred *p = &g->preds[i];
		int r = jw_get(buf, scan_len, p->key, val, sizeof(val));
		if (r != 1) return 0; /* not JSON or field missing */
		if (p->op == '=') {
			if (strcmp(val, p->val) != 0) return 0;
		} else { /* '~' */
			if (!p->re_ok) return 0;
			if (regexec(&p->re, val, 0, NULL, 0) != 0) return 0;
		}
	}
	where_emit_line(g, line_no, buf, len);
	return 0;
}

static void
cmd_log_where(struct session *s, struct cmd_args *a)
{
	unsigned mask;
	int streams_selected;
	struct where_pred *preds = NULL;
	int npred = 0;
	struct where_ctx gctx;
	int i;

	if (a->argc < 2) {
		session_writef(s,
		    "ERR usage: log_where KEY=VAL|KEY~REGEX ... [streams...]\n");
		return;
	}

	preds = calloc((size_t)a->argc, sizeof(*preds));
	if (!preds) {
		session_writef(s, "ERR internal: out of memory\n");
		return;
	}

	for (i = 1; i < a->argc; i++) {
		char *arg = a->argv[i];
		if (arg[0] == '-' && arg[1] == '-') continue;
		if (strcmp(arg, "stdout") == 0 || strcmp(arg, "stderr") == 0)
			continue;
		char *eq = strchr(arg, '=');
		char *tl = strchr(arg, '~');
		char *sep;
		int op;
		if (eq && (!tl || eq < tl)) { sep = eq; op = '='; }
		else if (tl)                { sep = tl; op = '~'; }
		else {
			session_writef(s,
			    "ERR usage: predicate '%s' missing = or ~\n", arg);
			goto cleanup;
		}
		*sep = '\0';
		preds[npred].key = arg;
		preds[npred].op = op;
		preds[npred].val = sep + 1;
		if (op == '~') {
			if (regcomp(&preds[npred].re, preds[npred].val,
			    REG_EXTENDED | REG_NOSUB) != 0) {
				session_writef(s, "ERR usage: bad regex: %s\n",
				    preds[npred].val);
				goto cleanup;
			}
			preds[npred].re_ok = 1;
		}
		npred++;
	}

	if (npred == 0) {
		session_writef(s,
		    "ERR usage: at least one KEY=VAL or KEY~REGEX required\n");
		goto cleanup;
	}

	mask = parse_streams(a, 1);
	streams_selected =
	    !!(mask & (1u << STREAM_STDOUT)) +
	    !!(mask & (1u << STREAM_STDERR));

	memset(&gctx, 0, sizeof(gctx));
	gctx.s = s;
	gctx.preds = preds;
	gctx.npred = npred;

	session_body_begin(s);

	if (mask & (1u << STREAM_STDOUT)) {
		gctx.stream_label = streams_selected > 1 ? "stdout" : NULL;
		gctx.last_emitted_line = 0;
		log_buf_for_each_line(&s->g->logs[STREAM_STDOUT],
		    where_each_line, &gctx);
	}
	if (mask & (1u << STREAM_STDERR)) {
		gctx.stream_label = streams_selected > 1 ? "stderr" : NULL;
		gctx.last_emitted_line = 0;
		log_buf_for_each_line(&s->g->logs[STREAM_STDERR],
		    where_each_line, &gctx);
	}

	session_body_end(s);

cleanup:
	for (i = 0; i < npred; i++) {
		if (preds[i].re_ok) regfree(&preds[i].re);
	}
	free(preds);
}

/*
 * log_jq: pipe JSON log lines through /usr/bin/jq.
 *
 * Only lines that start with '{' (i.e. produced by lud_logj) are fed to
 * jq; other lines are skipped so a legacy fprintf won't abort jq.  All
 * I/O is wired through libiox: stdin is drained as the pipe becomes
 * writable, stdout is forwarded to the session as it arrives, and stderr
 * is captured for an `ERR jq: ...` reply on non-zero exit.
 */

static void
jq_job_unlink(struct log_jq_job *j)
{
	struct log_jq_job **pp;

	for (pp = &g_jq_jobs; *pp; pp = &(*pp)->next) {
		if (*pp == j) {
			*pp = j->next;
			return;
		}
	}
}

static void
jq_close_fd(struct log_jq_job *j, int *fdp)
{
	if (*fdp >= 0) {
		iox_fd_remove(j->loop, *fdp);
		close(*fdp);
		*fdp = -1;
	}
}

static void
jq_job_finalize(struct log_jq_job *j)
{
	if (!j->reaped || j->stdout_fd >= 0 || j->stderr_fd >= 0)
		return;
	jq_close_fd(j, &j->stdin_fd);
	if (j->s) {
		if (WIFEXITED(j->exit_status) &&
		    WEXITSTATUS(j->exit_status) != 0) {
			/* trim trailing newline on errbuf */
			while (j->errlen > 0 &&
			    (j->errbuf[j->errlen - 1] == '\n' ||
			     j->errbuf[j->errlen - 1] == '\r'))
				j->errlen--;
			j->errbuf[j->errlen < sizeof(j->errbuf)
			    ? j->errlen : sizeof(j->errbuf) - 1] = '\0';
			session_body_end_err(j->s, "jq: %s",
			    j->errbuf[0] ? j->errbuf : "non-zero exit");
		} else if (WIFSIGNALED(j->exit_status)) {
			session_body_end_err(j->s, "jq: signal %d",
			    WTERMSIG(j->exit_status));
		} else {
			session_body_end(j->s);
		}
	}
	jq_job_unlink(j);
	free(j->inbuf);
	free(j);
}

static void
jq_kill_job(struct log_jq_job *j)
{
	if (j->pid > 0 && !j->reaped)
		kill(j->pid, SIGKILL);
	jq_close_fd(j, &j->stdin_fd);
	jq_close_fd(j, &j->stdout_fd);
	jq_close_fd(j, &j->stderr_fd);
}

static void
jq_on_writable(struct iox_loop *loop, int fd, unsigned events, void *arg)
{
	struct log_jq_job *j = arg;
	ssize_t n;

	(void)loop;
	(void)events;

	if (j->in_off >= j->in_size) {
		jq_close_fd(j, &j->stdin_fd);
		return;
	}
	n = write(fd, j->inbuf + j->in_off, j->in_size - j->in_off);
	if (n < 0) {
		if (errno == EAGAIN || errno == EINTR)
			return;
		/* EPIPE or similar: jq closed stdin early.  Drop writer. */
		jq_close_fd(j, &j->stdin_fd);
		return;
	}
	j->in_off += (size_t)n;
	if (j->in_off >= j->in_size)
		jq_close_fd(j, &j->stdin_fd);
}

static void
jq_on_stdout_readable(struct iox_loop *loop, int fd, unsigned events,
    void *arg)
{
	struct log_jq_job *j = arg;
	char buf[4096];
	ssize_t n;

	(void)loop;
	(void)events;

	n = read(fd, buf, sizeof(buf));
	if (n <= 0) {
		jq_close_fd(j, &j->stdout_fd);
		jq_job_finalize(j);
		return;
	}
	if (j->s) {
		session_body_write(j->s, buf, (size_t)n);
	}
}

static void
jq_on_stderr_readable(struct iox_loop *loop, int fd, unsigned events,
    void *arg)
{
	struct log_jq_job *j = arg;
	char buf[512];
	ssize_t n;
	size_t room;

	(void)loop;
	(void)events;

	n = read(fd, buf, sizeof(buf));
	if (n <= 0) {
		jq_close_fd(j, &j->stderr_fd);
		jq_job_finalize(j);
		return;
	}
	room = sizeof(j->errbuf) - j->errlen;
	if (room > 0) {
		size_t take = (size_t)n < room ? (size_t)n : room;
		memcpy(j->errbuf + j->errlen, buf, take);
		j->errlen += take;
	}
}

/* Collect log bytes from one stream; append only lines that look like
 * JSON objects (start with '{'). */
static size_t
jq_append_stream(const struct log_buf *b, char *out, size_t off, size_t cap)
{
	char *tmp;
	size_t n, i, start = 0;

	if (!b->data || b->len == 0)
		return off;
	tmp = malloc(b->len);
	if (!tmp)
		return off;
	n = log_buf_linearize(b, tmp, b->len);
	for (i = 0; i < n; i++) {
		if (tmp[i] == '\n') {
			size_t llen = i - start + 1;
			if (tmp[start] == '{' && off + llen <= cap) {
				memcpy(out + off, tmp + start, llen);
				off += llen;
			}
			start = i + 1;
		}
	}
	if (start < n && tmp[start] == '{') {
		size_t llen = n - start;
		if (off + llen + 1 <= cap) {
			memcpy(out + off, tmp + start, llen);
			off += llen;
			out[off++] = '\n';
		}
	}
	free(tmp);
	return off;
}

static void
cmd_log_jq(struct session *s, struct cmd_args *a)
{
	const char *expr = NULL;
	unsigned mask;
	int i;
	int in_pipe[2] = { -1, -1 };
	int out_pipe[2] = { -1, -1 };
	int err_pipe[2] = { -1, -1 };
	pid_t pid;
	struct log_jq_job *j = NULL;
	char *inbuf = NULL;
	size_t in_off = 0, cap;

	for (i = 1; i < a->argc; i++) {
		if (a->argv[i][0] != '-' && !expr) {
			expr = a->argv[i];
			break;
		}
	}
	if (!expr) {
		session_writef(s, "ERR usage: log_jq <jq-expr> [streams...]\n");
		return;
	}
	mask = parse_streams(a, i + 1);

	cap = g_log_bytes * 2 + 16;
	inbuf = malloc(cap);
	if (!inbuf) {
		session_writef(s, "ERR internal: out of memory\n");
		return;
	}
	if (mask & (1u << STREAM_STDOUT))
		in_off = jq_append_stream(&s->g->logs[STREAM_STDOUT],
		    inbuf, in_off, cap);
	if (mask & (1u << STREAM_STDERR))
		in_off = jq_append_stream(&s->g->logs[STREAM_STDERR],
		    inbuf, in_off, cap);

	if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
		session_writef(s, "ERR internal: pipe: %s\n", strerror(errno));
		goto fail;
	}

	pid = fork();
	if (pid < 0) {
		session_writef(s, "ERR internal: fork: %s\n", strerror(errno));
		goto fail;
	}
	if (pid == 0) {
		dup2(in_pipe[0], 0);
		dup2(out_pipe[1], 1);
		dup2(err_pipe[1], 2);
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);
		close(err_pipe[0]); close(err_pipe[1]);
		execlp("jq", "jq", "-c", expr, (char *)0);
		_exit(127);
	}

	close(in_pipe[0]);  in_pipe[0] = -1;
	close(out_pipe[1]); out_pipe[1] = -1;
	close(err_pipe[1]); err_pipe[1] = -1;

	/* Non-blocking I/O on all three pipes. */
	fcntl(in_pipe[1],  F_SETFL, O_NONBLOCK);
	fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
	fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

	j = calloc(1, sizeof(*j));
	if (!j) {
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		session_writef(s, "ERR internal: out of memory\n");
		goto fail;
	}
	j->s = s;
	j->loop = s->loop;
	j->pid = pid;
	j->stdin_fd  = in_pipe[1];
	j->stdout_fd = out_pipe[0];
	j->stderr_fd = err_pipe[0];
	j->inbuf = inbuf;
	j->in_size = in_off;
	j->next = g_jq_jobs;
	g_jq_jobs = j;

	session_body_begin(s);

	iox_fd_add(s->loop, j->stdout_fd, IOX_READ,
	    jq_on_stdout_readable, j);
	iox_fd_add(s->loop, j->stderr_fd, IOX_READ,
	    jq_on_stderr_readable, j);
	if (j->in_size > 0) {
		iox_fd_add(s->loop, j->stdin_fd, IOX_WRITE,
		    jq_on_writable, j);
	} else {
		close(j->stdin_fd);
		j->stdin_fd = -1;
	}
	return;

fail:
	if (in_pipe[0]  >= 0) close(in_pipe[0]);
	if (in_pipe[1]  >= 0) close(in_pipe[1]);
	if (out_pipe[0] >= 0) close(out_pipe[0]);
	if (out_pipe[1] >= 0) close(out_pipe[1]);
	if (err_pipe[0] >= 0) close(err_pipe[0]);
	if (err_pipe[1] >= 0) close(err_pipe[1]);
	free(inbuf);
}

static void
cmd_env(struct session *s, struct cmd_args *a)
{
	const char *v;

	if (a->argc < 2) {
		session_writef(s, "ERR usage: env <KEY> [VALUE]\n");
		return;
	}
	if (a->argc >= 3) {
		if (setenv(a->argv[1], a->argv[2], 1) < 0) {
			session_writef(s, "ERR internal: setenv: %s\n",
			    strerror(errno));
			return;
		}
		session_writef(s, "OK %s=%s\n", a->argv[1], a->argv[2]);
		return;
	}
	v = getenv(a->argv[1]);
	session_writef(s, "OK %s=%s\n", a->argv[1], v ? v : "");
}

static void
cmd_unsetenv(struct session *s, struct cmd_args *a)
{
	if (a->argc < 2) {
		session_writef(s, "ERR usage: unsetenv <KEY>\n");
		return;
	}
	unsetenv(a->argv[1]);
	session_writef(s, "OK\n");
}

static void
cmd_log_clear(struct session *s, struct cmd_args *a)
{
	unsigned mask = parse_streams(a, 1);

	if (mask & (1u << STREAM_STDOUT))
		log_buf_clear(&s->g->logs[STREAM_STDOUT]);
	if (mask & (1u << STREAM_STDERR))
		log_buf_clear(&s->g->logs[STREAM_STDERR]);
	session_writef(s, "OK cleared\n");
}

static void
cmd_session_info(struct session *s, struct cmd_args *a)
{
	(void)a;
	if (!s->g) {
		session_writef(s, "ERR no_session\n");
		return;
	}
	session_writef(s, "OK id=%d name=%s attached=yes nokill=%s\n",
	    s->g->id,
	    s->g->name[0] ? s->g->name : "-",
	    s->g->nokill ? "yes" : "no");
}

static void
cmd_session_list(struct session *s, struct cmd_args *a)
{
	int i, first = 1;
	char buf[1024];
	size_t off = 0;

	(void)a;

	off += (size_t)snprintf(buf + off, sizeof(buf) - off, "OK");
	for (i = 0; i < MAX_SESSIONS; i++) {
		const struct gsession *g = g_gsessions[i];
		const char *name;
		if (!g)
			continue;
		name = g->name[0] ? g->name : "-";
		if (off < sizeof(buf)) {
			off += (size_t)snprintf(buf + off, sizeof(buf) - off,
			    "%sid=%d,name=%s,attached=%s,nokill=%s",
			    first ? " " : " ",
			    g->id, name,
			    g->owner ? "yes" : "no",
			    g->nokill ? "yes" : "no");
			first = 0;
		}
	}
	if (first)
		session_writef(s, "OK (no sessions)\n");
	else
		session_writef(s, "%s\n", buf);
}

static void
cmd_session_name(struct session *s, struct cmd_args *a)
{
	if (a->argc < 2) {
		session_writef(s, "ERR usage: session_name <name>\n");
		return;
	}
	if (!s->g) {
		session_writef(s, "ERR no_session\n");
		return;
	}
	if (strlen(a->argv[1]) >= sizeof(s->g->name)) {
		session_writef(s, "ERR usage: name too long\n");
		return;
	}
	/* reject names that collide with another live gsession */
	{
		struct gsession *other = gsession_by_name(a->argv[1]);
		if (other && other != s->g) {
			session_writef(s,
			    "ERR not_allowed: name %s already in use\n",
			    a->argv[1]);
			return;
		}
	}
	snprintf(s->g->name, sizeof(s->g->name), "%s", a->argv[1]);
	session_writef(s, "OK\n");
}

static void
cmd_session_nokill(struct session *s, struct cmd_args *a)
{
	(void)a;
	if (!s->g) {
		session_writef(s, "ERR no_session\n");
		return;
	}
	s->g->nokill = 1;
	session_writef(s, "OK\n");
}

static void
cmd_session_detach(struct session *s, struct cmd_args *a)
{
	(void)a;
	if (!s->g) {
		session_writef(s, "ERR no_session\n");
		return;
	}
	/* Acknowledge, then drop the connection.  session_close() will
	 * preserve the gsession iff nokill was set; otherwise the game
	 * is killed as usual. */
	session_writef(s, "OK detached\n");
	s->closed = 1;
}

static void
cmd_session_attach(struct session *s, struct cmd_args *a)
{
	struct gsession *target;

	if (a->argc < 2) {
		session_writef(s, "ERR usage: session_attach <name>\n");
		return;
	}
	target = gsession_by_name(a->argv[1]);
	if (!target) {
		session_writef(s, "ERR not_allowed: no such session: %s\n",
		    a->argv[1]);
		return;
	}
	if (target == s->g) {
		session_writef(s, "OK attached (already)\n");
		return;
	}
	if (target->owner) {
		session_writef(s,
		    "ERR not_allowed: session %s already attached\n",
		    a->argv[1]);
		return;
	}
	/* Swap: destroy our current gsession (if unused & not nokill),
	 * then adopt the target. */
	if (s->g) {
		struct gsession *old = s->g;
		old->owner = NULL;
		if (!old->nokill)
			gsession_destroy(old);
	}
	s->g = target;
	target->owner = s;
	session_writef(s, "OK attached\n");
}

static void
cmd_session_kill(struct session *s, struct cmd_args *a)
{
	struct gsession *target;

	if (a->argc < 2) {
		session_writef(s, "ERR usage: session_kill <name>\n");
		return;
	}
	target = gsession_by_name(a->argv[1]);
	if (!target) {
		session_writef(s, "ERR not_allowed: no such session: %s\n",
		    a->argv[1]);
		return;
	}
	if (target == s->g) {
		/* killing our own session: detach first so we don't UAF */
		s->g = NULL;
		target->owner = NULL;
	} else if (target->owner) {
		/* another connection owns it -- orphan that connection's
		 * gsession pointer so its next access reports no_session */
		target->owner->g = NULL;
		target->owner = NULL;
	}
	gsession_destroy(target);
	session_writef(s, "OK\n");
}

/* Forward a Control-subset command verbatim over ctl_fd, enqueue the
 * originating session so the reply line routes back on pop.  All
 * control commands share this handler because the game side speaks
 * the same line-oriented text protocol -- the launcher is just a
 * dumb forwarder for this subset. */
static void
cmd_proxy(struct session *s, struct cmd_args *a)
{
	char line[LINE_MAX_BYTES];
	int off = 0, i;
	ssize_t w;

	if (!s->g || s->g->ctl_fd < 0) {
		session_writef(s, "ERR no_control\n");
		return;
	}
	if (s->g->ctl_q_count >= CTL_Q_CAP) {
		session_writef(s, "ERR busy: control queue full\n");
		return;
	}
	for (i = 0; i < a->argc; i++) {
		int n = snprintf(line + off, sizeof(line) - (size_t)off,
		    "%s%s", i ? " " : "", a->argv[i]);
		if (n < 0 || off + n >= (int)sizeof(line) - 1) {
			session_writef(s, "ERR usage: command too long\n");
			return;
		}
		off += n;
	}
	line[off++] = '\n';

	w = write(s->g->ctl_fd, line, (size_t)off);
	if (w != off) {
		session_writef(s, "ERR control_write: %s\n",
		    w < 0 ? strerror(errno) : "short write");
		ctl_close(s->g);
		return;
	}
	ctl_q_push(s->g, s);
}

/* ========== gdb / core helpers (Phase 5) ========== */

/* Fork/exec argv, read stdout into `out` up to cap-1 bytes (NUL-terminated),
 * discard stderr, wait for exit.  Returns number of bytes read (>=0) on
 * exec success, -1 on fork/pipe/exec failure.  Overflow beyond cap is
 * silently dropped.
 *
 * The global SIGCHLD handler (on_sigchld) uses waitpid(-1, WNOHANG) and
 * skips pids it doesn't recognize, so our own waitpid(pid,...) here still
 * succeeds under normal scheduling.
 */
static ssize_t
run_capture_argv(char *const argv[], char *out, size_t cap)
{
	int p[2];
	pid_t pid;
	ssize_t total = 0, n;
	int status;
	int devnull;

	if (!cap)
		return -1;
	if (pipe(p) < 0)
		return -1;
	pid = fork();
	if (pid < 0) {
		close(p[0]); close(p[1]);
		return -1;
	}
	if (pid == 0) {
		close(p[0]);
		dup2(p[1], STDOUT_FILENO);
		devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		close(p[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(p[1]);
	for (;;) {
		if (cap > 0 && (size_t)total >= cap - 1)
			break;
		n = read(p[0], out + total, cap - 1 - (size_t)total);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
		total += n;
	}
	/* if the child wrote more than we had room for, drain so it exits */
	if (cap > 0 && (size_t)total >= cap - 1) {
		char sink[4096];
		while ((n = read(p[0], sink, sizeof(sink))) > 0)
			;
	}
	close(p[0]);
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	out[total] = '\0';
	return total;
}

/* Locate the most recent core file for the given binary path.  Strategy:
 *
 *   1. Read /proc/sys/kernel/core_pattern.  If it starts with '|', parse
 *      the pipe helper name (basename of the first whitespace-delimited
 *      token).
 *   2. If the helper is systemd-coredump, shell out to `coredumpctl` to
 *      find the most recent matching core for the binary's basename.
 *   3. If the helper is apport, scan /var/lib/apport/coredump/ for
 *      core.<basename>.*.
 *   4. Otherwise treat core_pattern as a filesystem pattern and check
 *      CWD, /var/crash, and / — for each, expand %e to basename and
 *      glob %p.  We accept the most recently modified match.
 *
 * Returns 0 on success (writing into out), -1 on no match.
 */
static int
core_locate_for(const char *binary, pid_t pid, char *out, size_t cap)
{
	FILE *fp;
	char pattern[512];
	char base[PATH_MAX];
	const char *slash;
	int found = -1;

	(void)pid;

	slash = strrchr(binary, '/');
	snprintf(base, sizeof(base), "%s", slash ? slash + 1 : binary);

	pattern[0] = '\0';
	fp = fopen("/proc/sys/kernel/core_pattern", "r");
	if (fp) {
		if (fgets(pattern, sizeof(pattern), fp)) {
			size_t n = strlen(pattern);
			while (n > 0 && (pattern[n - 1] == '\n' ||
			    pattern[n - 1] == ' '))
				pattern[--n] = '\0';
		}
		fclose(fp);
	}

	/* case 1: piped to systemd-coredump */
	if (strstr(pattern, "systemd-coredump")) {
		char *argv[] = {
			(char *)"coredumpctl", (char *)"-1",
			(char *)"--no-legend", (char *)"--no-pager",
			(char *)"info", base, NULL
		};
		char buf[8192];
		char *line, *saveptr;
		ssize_t got;

		got = run_capture_argv(argv, buf, sizeof(buf));
		if (got > 0) {
			for (line = strtok_r(buf, "\n", &saveptr); line;
			    line = strtok_r(NULL, "\n", &saveptr)) {
				const char *key = "Storage:";
				char *p = strstr(line, key);
				char *end;
				if (!p)
					continue;
				p += strlen(key);
				while (*p == ' ' || *p == '\t')
					p++;
				end = p + strlen(p);
				while (end > p && (end[-1] == ' ' ||
				    end[-1] == '\t'))
					end--;
				/* strip a trailing " (present)" or similar */
				{
					char *paren = strchr(p, '(');
					if (paren && paren < end) {
						end = paren;
						while (end > p && end[-1] == ' ')
							end--;
					}
				}
				if (end > p) {
					size_t len = (size_t)(end - p);
					if (len >= cap)
						len = cap - 1;
					memcpy(out, p, len);
					out[len] = '\0';
					if (access(out, R_OK) == 0)
						found = 0;
				}
				break;
			}
		}
		if (found == 0)
			return 0;
	}

	/* case 2: piped to apport */
	if (strstr(pattern, "apport")) {
		char prefix[256];
		DIR *dir;
		struct dirent *de;
		struct stat st;
		time_t best_mt = 0;
		char best[PATH_MAX];

		best[0] = '\0';
		snprintf(prefix, sizeof(prefix), "core.%.200s.", base);
		dir = opendir("/var/lib/apport/coredump");
		if (dir) {
			while ((de = readdir(dir)) != NULL) {
				char full[PATH_MAX];
				if (strncmp(de->d_name, prefix,
				    strlen(prefix)) != 0)
					continue;
				snprintf(full, sizeof(full),
				    "/var/lib/apport/coredump/%s",
				    de->d_name);
				if (stat(full, &st) == 0 &&
				    st.st_mtime > best_mt) {
					best_mt = st.st_mtime;
					snprintf(best, sizeof(best), "%s",
					    full);
				}
			}
			closedir(dir);
		}
		if (best[0]) {
			snprintf(out, cap, "%s", best);
			return 0;
		}
	}

	/* case 3: literal pattern — look in a handful of likely dirs */
	{
		static const char *dirs[] = {
			".", "/var/crash", NULL
		};
		int i;
		struct stat st;
		time_t best_mt = 0;
		char best[PATH_MAX];
		DIR *dir;
		struct dirent *de;

		best[0] = '\0';
		for (i = 0; dirs[i]; i++) {
			dir = opendir(dirs[i]);
			if (!dir)
				continue;
			while ((de = readdir(dir)) != NULL) {
				char full[PATH_MAX];
				const char *n = de->d_name;
				if (strncmp(n, "core", 4) != 0)
					continue;
				if (n[4] != '\0' && n[4] != '.')
					continue;
				/* if filename contains the basename, prefer it */
				snprintf(full, sizeof(full), "%s/%s",
				    dirs[i], n);
				if (stat(full, &st) != 0)
					continue;
				/* skip if name contains a different basename */
				if (n[4] == '.' && strstr(n, base) == NULL)
					continue;
				if (st.st_mtime > best_mt) {
					best_mt = st.st_mtime;
					snprintf(best, sizeof(best), "%s",
					    full);
				}
			}
			closedir(dir);
		}
		if (best[0]) {
			snprintf(out, cap, "%s", best);
			return 0;
		}
	}

	return -1;
}

/* Is this stack frame function part of the crash noise we skip when
 * computing a summary? */
static int
crash_frame_is_noise(const char *func)
{
	static const char *noise[] = {
		"raise", "abort", "__assert_fail", "__assert_fail_base",
		"__restore_rt", "_start", "start_thread", "__libc_start_main",
		NULL
	};
	int i;

	if (!func || !*func)
		return 1;
	for (i = 0; noise[i]; i++) {
		if (strcmp(func, noise[i]) == 0)
			return 1;
	}
	if (strncmp(func, "__GI_", 5) == 0)
		return 1;
	if (strncmp(func, "__libc_", 7) == 0)
		return 1;
	if (strncmp(func, "__pthread_kill", 14) == 0)
		return 1;
	if (strstr(func, "pthread_kill"))
		return 1;
	return 0;
}

/* Extract the function name from a gdb "bt" frame line.  Lines look like:
 *
 *   #0  0x000055... in draw_sector_recursive (sec=0x...) at src/hero/hero.c:214
 *   #5  __GI_raise (sig=11) at ../sysdeps/unix/sysv/linux/raise.c:51
 *
 * Writes the function name into func (cap bytes), the file:line into
 * where (cap bytes); empty strings if absent.  Returns 1 if parse found
 * anything, 0 otherwise. */
static int
parse_gdb_frame_line(const char *line, char *func, size_t fcap,
    char *where, size_t wcap)
{
	const char *p;
	const char *fstart, *fend;
	const char *at;
	size_t n;

	func[0] = '\0';
	where[0] = '\0';

	if (line[0] != '#')
		return 0;

	/* skip "#N " */
	p = line + 1;
	while (*p && *p != ' ')
		p++;
	while (*p == ' ')
		p++;

	/* optional "0x... in " prefix */
	if (p[0] == '0' && p[1] == 'x') {
		const char *space = strchr(p, ' ');
		if (space && strncmp(space, " in ", 4) == 0)
			p = space + 4;
		else
			return 0;
	}

	/* function name runs up to " (" */
	fstart = p;
	fend = strstr(p, " (");
	if (!fend)
		return 0;
	n = (size_t)(fend - fstart);
	if (n >= fcap)
		n = fcap - 1;
	memcpy(func, fstart, n);
	func[n] = '\0';

	at = strstr(fend, ") at ");
	if (at) {
		at += 5;
		n = strlen(at);
		if (n >= wcap)
			n = wcap - 1;
		memcpy(where, at, n);
		where[n] = '\0';
	}
	return 1;
}

/* If core_in ends in .zst, decompress it to a temp file and store the
 * path in tmp_out (cap bytes).  Returns the path to use with gdb.
 * Caller should unlink tmp_out if non-empty. */
static const char *
decompress_core_if_needed(const char *core_in, char *tmp_out, size_t cap)
{
	const char *ext;
	char *argv[8];
	int fd;
	pid_t pid;
	int status;

	tmp_out[0] = '\0';
	ext = strrchr(core_in, '.');
	if (!ext || strcmp(ext, ".zst") != 0)
		return core_in;

	snprintf(tmp_out, cap, "/tmp/ludica-core.XXXXXX");
	fd = mkstemp(tmp_out);
	if (fd < 0) {
		tmp_out[0] = '\0';
		return core_in;
	}

	pid = fork();
	if (pid == 0) {
		dup2(fd, 1);
		close(fd);
		argv[0] = (char *)"zstd";
		argv[1] = (char *)"-dcq";
		argv[2] = (char *)core_in;
		argv[3] = NULL;
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fd);
	if (pid < 0) {
		unlink(tmp_out);
		tmp_out[0] = '\0';
		return core_in;
	}
	if (waitpid(pid, &status, 0) < 0 ||
	    !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		unlink(tmp_out);
		tmp_out[0] = '\0';
		return core_in;
	}
	return tmp_out;
}

/* Run `gdb -batch -nx <gdb_cmds> <binary> <core>` and capture stdout.
 * cmds is a NULL-terminated array of "-ex"/<command> pairs; the caller
 * supplies only the commands, we wrap with the setup.  Returns bytes
 * read, or -1 on failure. */
static ssize_t
run_gdb_batch(const char *binary, const char *core, const char *const *cmds,
    char *out, size_t cap)
{
	char *argv[64];
	char tmp[64];
	const char *core_path;
	ssize_t r;
	int n = 0;
	int i;

	core_path = decompress_core_if_needed(core, tmp, sizeof(tmp));

	argv[n++] = (char *)"gdb";
	argv[n++] = (char *)"-batch";
	argv[n++] = (char *)"-nx";
	argv[n++] = (char *)"-ex";
	argv[n++] = (char *)"set pagination off";
	argv[n++] = (char *)"-ex";
	argv[n++] = (char *)"set print thread-events off";
	for (i = 0; cmds[i] && n < 60; i++) {
		argv[n++] = (char *)"-ex";
		argv[n++] = (char *)cmds[i];
	}
	argv[n++] = (char *)binary;
	argv[n++] = (char *)core_path;
	argv[n] = NULL;
	r = run_capture_argv(argv, out, cap);
	if (tmp[0])
		unlink(tmp);
	return r;
}

/* Compute a one-line crash summary by walking the backtrace and picking
 * the top non-noise frame.  Writes into out; returns 0 on success. */
static int
compute_crash_summary(const char *binary, const char *core, int sig,
    char *out, size_t cap)
{
	static char buf[64 * 1024];
	const char *cmds[] = { "bt 50", NULL };
	ssize_t got;
	char *line, *saveptr;
	char func[256], where[PATH_MAX + 64];
	const char *signame;
	int found = 0;

	got = run_gdb_batch(binary, core, cmds, buf, sizeof(buf));
	if (got <= 0)
		return -1;

	for (line = strtok_r(buf, "\n", &saveptr); line;
	    line = strtok_r(NULL, "\n", &saveptr)) {
		if (line[0] != '#')
			continue;
		if (!parse_gdb_frame_line(line, func, sizeof(func),
		    where, sizeof(where)))
			continue;
		if (crash_frame_is_noise(func))
			continue;
		found = 1;
		break;
	}
	if (!found) {
		func[0] = '\0';
		where[0] = '\0';
	}

	switch (sig) {
	case SIGSEGV: signame = "SIGSEGV"; break;
	case SIGABRT: signame = "SIGABRT"; break;
	case SIGBUS:  signame = "SIGBUS";  break;
	case SIGFPE:  signame = "SIGFPE";  break;
	case SIGILL:  signame = "SIGILL";  break;
	case 0:       signame = "";        break;
	default:      signame = NULL;      break;
	}

	if (where[0] && func[0] && signame && signame[0])
		snprintf(out, cap, "%s in %s: %s", where, func, signame);
	else if (where[0] && func[0])
		snprintf(out, cap, "%s in %s", where, func);
	else if (func[0] && signame && signame[0])
		snprintf(out, cap, "%s: %s", func, signame);
	else if (signame && signame[0])
		snprintf(out, cap, "crash: %s", signame);
	else
		snprintf(out, cap, "crash (no frames resolved)");
	return 0;
}

/* Resolve the core path to use for a gdb_core_* command: either the one
 * passed via --core=PATH or the cached one.  Returns NULL if neither. */
static const char *
resolve_core_arg(struct session *s, struct cmd_args *a)
{
	static char override[PATH_MAX];
	int i;

	for (i = 1; i < a->argc; i++) {
		if (strncmp(a->argv[i], "--core=", 7) == 0) {
			snprintf(override, sizeof(override), "%s",
			    a->argv[i] + 7);
			return override;
		}
	}
	if (s->g->core_path[0])
		return s->g->core_path;
	return NULL;
}

static int
parse_named_int(struct cmd_args *a, const char *key, int *out)
{
	size_t klen = strlen(key);
	int i;
	for (i = 1; i < a->argc; i++) {
		if (strncmp(a->argv[i], key, klen) == 0 &&
		    a->argv[i][klen] == '=') {
			return parse_positive_int(a->argv[i] + klen + 1, out);
		}
	}
	return -1;
}

static void
cmd_gdb_hint(struct session *s, struct cmd_args *a)
{
	(void)a;
	if (s->g->pstate != PROC_RUNNING || s->g->pid <= 0) {
		session_writef(s, "ERR no_process\n");
		return;
	}
	session_writef(s, "OK pid=%d suggested: gdb -p %d %s\n",
	    (int)s->g->pid, (int)s->g->pid, s->g->bin_path);
}

static void
cmd_gdb_core_find(struct session *s, struct cmd_args *a)
{
	char path[PATH_MAX];

	(void)a;
	if (s->g->core_path[0]) {
		session_writef(s, "OK path=%s\n", s->g->core_path);
		return;
	}
	if (s->g->bin_path[0] == '\0') {
		session_writef(s, "ERR no_core: no binary has been spawned\n");
		return;
	}
	if (core_locate_for(s->g->bin_path, s->g->pid, path, sizeof(path)) < 0) {
		session_writef(s, "ERR no_core: no core file found for %s\n",
		    s->g->bin_path);
		return;
	}
	snprintf(s->g->core_path, sizeof(s->g->core_path), "%s", path);
	session_writef(s, "OK path=%s\n", s->g->core_path);
}

static void
cmd_gdb_core_list(struct session *s, struct cmd_args *a)
{
	char *argv[] = {
		(char *)"coredumpctl", (char *)"--no-legend",
		(char *)"--no-pager", (char *)"list", NULL, NULL
	};
	static char buf[32 * 1024];
	const char *slash;
	char base[PATH_MAX];
	ssize_t got;

	(void)a;
	if (s->g->bin_path[0] == '\0') {
		session_writef(s, "ERR no_core: no binary has been spawned\n");
		return;
	}
	slash = strrchr(s->g->bin_path, '/');
	snprintf(base, sizeof(base), "%s", slash ? slash + 1 : s->g->bin_path);
	argv[4] = base;
	got = run_capture_argv(argv, buf, sizeof(buf));
	if (got <= 0) {
		session_writef(s, "ERR no_core: coredumpctl returned nothing\n");
		return;
	}
	session_body_begin(s);
	write_log_chunk(s, NULL, buf, (size_t)got);
	session_body_end(s);
}

static void
cmd_gdb_core_summary(struct session *s, struct cmd_args *a)
{
	const char *core;
	int sig;

	core = resolve_core_arg(s, a);
	if (!core) {
		session_writef(s, "ERR no_core: no core path cached; "
		    "run gdb_core_find first or pass --core=PATH\n");
		return;
	}
	if (s->g->crash_summary[0] && core == s->g->core_path) {
		session_writef(s, "OK %s\n", s->g->crash_summary);
		return;
	}
	sig = s->g->pstate == PROC_SIGNALED ? s->g->exit_signal : 0;
	if (compute_crash_summary(s->g->bin_path, core, sig,
	    s->g->crash_summary, sizeof(s->g->crash_summary)) < 0) {
		session_writef(s, "ERR internal: gdb invocation failed\n");
		return;
	}
	session_writef(s, "OK %s\n", s->g->crash_summary);
}

static void
cmd_gdb_core_backtrace(struct session *s, struct cmd_args *a)
{
	const char *core;
	int limit = 50;
	char btcmd[32];
	const char *cmds[2];
	static char buf[256 * 1024];
	ssize_t got;

	core = resolve_core_arg(s, a);
	if (!core) {
		session_writef(s, "ERR no_core: no core path cached; "
		    "run gdb_core_find first or pass --core=PATH\n");
		return;
	}
	parse_named_int(a, "--limit", &limit);
	if (limit < 1) limit = 1;
	if (limit > 1000) limit = 1000;
	snprintf(btcmd, sizeof(btcmd), "bt %d", limit);
	cmds[0] = btcmd;
	cmds[1] = NULL;
	got = run_gdb_batch(s->g->bin_path, core, cmds, buf, sizeof(buf));
	if (got <= 0) {
		session_writef(s, "ERR internal: gdb invocation failed\n");
		return;
	}
	session_body_begin(s);
	write_log_chunk(s, NULL, buf, (size_t)got);
	session_body_end(s);
}

static void
cmd_gdb_core_frame(struct session *s, struct cmd_args *a)
{
	const char *core;
	int frame;
	char fcmd[64], icmd[64];
	const char *cmds[3];
	static char buf[64 * 1024];
	ssize_t got;

	{
		int i, have = 0;
		char *end;
		long v;
		for (i = 1; i < a->argc; i++) {
			const char *arg = a->argv[i];
			const char *num = NULL;
			if (strncmp(arg, "--frame=", 8) == 0)
				num = arg + 8;
			else if (arg[0] != '-' && !have)
				num = arg;
			if (!num)
				continue;
			v = strtol(num, &end, 10);
			if (*num == '\0' || *end != '\0' || v < 0 ||
			    v > INT_MAX) {
				session_writef(s, "ERR usage: gdb_core_frame "
				    "<n> [--core=PATH]  (or --frame=N)\n");
				return;
			}
			frame = (int)v;
			have = 1;
		}
		if (!have) {
			session_writef(s, "ERR usage: gdb_core_frame <n> "
			    "[--core=PATH]  (or --frame=N)\n");
			return;
		}
	}
	core = resolve_core_arg(s, a);
	if (!core) {
		session_writef(s, "ERR no_core: no core path cached; "
		    "run gdb_core_find first or pass --core=PATH\n");
		return;
	}
	snprintf(fcmd, sizeof(fcmd), "frame %d", frame);
	snprintf(icmd, sizeof(icmd), "info frame");
	cmds[0] = fcmd;
	cmds[1] = icmd;
	cmds[2] = NULL;
	got = run_gdb_batch(s->g->bin_path, core, cmds, buf, sizeof(buf));
	if (got <= 0) {
		session_writef(s, "ERR internal: gdb invocation failed\n");
		return;
	}
	session_body_begin(s);
	write_log_chunk(s, NULL, buf, (size_t)got);
	session_body_end(s);
}

static void
cmd_gdb_core_locals(struct session *s, struct cmd_args *a)
{
	const char *core;
	int frame = 0;
	char fcmd[64];
	const char *cmds[3];
	static char buf[64 * 1024];
	ssize_t got;

	core = resolve_core_arg(s, a);
	if (!core) {
		session_writef(s, "ERR no_core: no core path cached; "
		    "run gdb_core_find first or pass --core=PATH\n");
		return;
	}
	parse_named_int(a, "--frame", &frame);
	if (frame < 0) frame = 0;
	snprintf(fcmd, sizeof(fcmd), "frame %d", frame);
	cmds[0] = fcmd;
	cmds[1] = "info locals";
	cmds[2] = NULL;
	got = run_gdb_batch(s->g->bin_path, core, cmds, buf, sizeof(buf));
	if (got <= 0) {
		session_writef(s, "ERR internal: gdb invocation failed\n");
		return;
	}
	session_body_begin(s);
	write_log_chunk(s, NULL, buf, (size_t)got);
	session_body_end(s);
}

/* Phase 6: map an event name (or "*") to a bitmask.  Returns 0 on
 * unknown. */
static unsigned
event_flag_by_name(const char *name)
{
	if (strcmp(name, "*") == 0 || strcmp(name, "all") == 0)
		return EV_ALL;
	if (strcmp(name, "log_stdout_line") == 0) return EV_LOG_STDOUT_LINE;
	if (strcmp(name, "log_stderr_line") == 0) return EV_LOG_STDERR_LINE;
	if (strcmp(name, "process_exit") == 0)    return EV_PROCESS_EXIT;
	if (strcmp(name, "crash") == 0)           return EV_CRASH;
	if (strcmp(name, "crash_summary") == 0)   return EV_CRASH_SUMMARY;
	return 0;
}

static void
cmd_subscribe(struct session *s, struct cmd_args *a)
{
	unsigned mask;

	if (a->argc < 2) {
		session_writef(s, "ERR usage: subscribe <event>|*\n");
		return;
	}
	mask = event_flag_by_name(a->argv[1]);
	if (!mask) {
		session_writef(s,
		    "ERR usage: unknown event '%s' "
		    "(log_stdout_line|log_stderr_line|process_exit|crash|crash_summary|*)\n",
		    a->argv[1]);
		return;
	}
	s->sub_flags |= mask;
	session_writef(s, "OK subscribed\n");
}

static void
cmd_unsubscribe(struct session *s, struct cmd_args *a)
{
	if (a->argc >= 2) {
		unsigned mask = event_flag_by_name(a->argv[1]);
		if (!mask) {
			session_writef(s, "ERR usage: unknown event '%s'\n",
			    a->argv[1]);
			return;
		}
		s->sub_flags &= ~mask;
	} else {
		s->sub_flags = 0;
	}
	session_writef(s, "OK\n");
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
	{ "spawn",        cmd_spawn,        "fork/exec an allowlisted binary" },
	{ "kill",         cmd_kill,         "send SIGTERM (or given signal) to child" },
	{ "log_tail",     cmd_log_tail,     "emit last N lines [streams...]" },
	{ "log_head",     cmd_log_head,     "emit first N lines [streams...]" },
	{ "log_range",    cmd_log_range,    "emit lines FIRST..LAST [streams...]" },
	{ "log_grep",     cmd_log_grep,     "regex search [--ctx=N] PATTERN [streams...]" },
	{ "log_where",    cmd_log_where,    "structural filter KEY=VAL|KEY~REGEX ... [streams...]" },
	{ "log_jq",       cmd_log_jq,       "pipe JSON log through jq EXPR [streams...]" },
	{ "log_clear",    cmd_log_clear,    "clear log buffer [streams...]" },
	{ "env",          cmd_env,          "get/set env var: env KEY [VALUE]" },
	{ "unsetenv",     cmd_unsetenv,     "unset env var: unsetenv KEY" },
	{ "session_info",   cmd_session_info,   "report current session identity" },
	{ "session_list",   cmd_session_list,   "list all active sessions globally" },
	{ "session_name",   cmd_session_name,   "assign a name to the current session" },
	{ "session_nokill", cmd_session_nokill, "mark session so game outlives disconnect" },
	{ "session_detach", cmd_session_detach, "detach connection (closes); game survives if nokill" },
	{ "session_attach", cmd_session_attach, "attach to an existing named session" },
	{ "session_kill",   cmd_session_kill,   "force-destroy a named session" },
	{ "action",       cmd_proxy,        "proxy: press/hold/release a named action" },
	{ "step",         cmd_proxy,        "proxy: advance N frames (default 1)" },
	{ "pause",        cmd_proxy,        "proxy: pause the game loop" },
	{ "resume",       cmd_proxy,        "proxy: resume the game loop" },
	{ "seed",         cmd_proxy,        "proxy: set deterministic RNG seed" },
	{ "screenshot",   cmd_proxy,        "proxy: capture a screenshot" },
	{ "read_pixel",   cmd_proxy,        "proxy: read a pixel at X Y" },
	{ "query",        cmd_proxy,        "proxy: query frame|size|fps|var" },
	{ "list_actions", cmd_proxy,        "proxy: list registered actions" },
	{ "list_vars",    cmd_proxy,        "proxy: list registered state vars" },
	{ "gdb_hint",          cmd_gdb_hint,          "return PID + suggested `gdb -p` command" },
	{ "gdb_core_find",     cmd_gdb_core_find,     "locate most recent core for the spawned binary" },
	{ "gdb_core_list",     cmd_gdb_core_list,     "list cores (via coredumpctl)" },
	{ "gdb_core_summary",  cmd_gdb_core_summary,  "one-line crash summary [--core=PATH]" },
	{ "gdb_core_backtrace",cmd_gdb_core_backtrace,"backtrace [--core=PATH] [--limit=N]" },
	{ "gdb_core_frame",    cmd_gdb_core_frame,    "info for frame N (positional or --frame=N) [--core=PATH]" },
	{ "gdb_core_locals",   cmd_gdb_core_locals,   "local variables [--core=PATH] [--frame=N]" },
	{ "subscribe",         cmd_subscribe,         "subscribe to event stream (EVENT lines)" },
	{ "unsubscribe",       cmd_unsubscribe,       "unsubscribe from events (all or one)" },
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

	session_body_begin(s);
	session_body_writef(s, "commands:\n");
	for (c = commands; c->name; c++)
		session_body_writef(s, " %-16s %s\n", c->name, c->summary);
	session_body_end(s);
}

static void
dispatch(struct session *s, char *line)
{
	struct cmd_args args;
	const struct command *c;

	parse_args(line, &args);
	if (args.argc == 0)
		return;

	/* Re-establish a gsession if ours was destroyed (e.g. another
	 * connection issued session_kill on it).  Keeps the invariant
	 * that command handlers can rely on s->g being non-NULL. */
	if (!s->g) {
		struct gsession *g = gsession_new(s->loop);
		if (!g) {
			session_writef(s, "ERR internal: gsession_new failed\n");
			return;
		}
		s->g = g;
		g->owner = s;
	}

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
		if (s->closed) {
			session_close(s);
			return;
		}

		i = (int)(nl - s->linebuf) + 1;
		s->linelen -= i;
		if (s->linelen > 0)
			memmove(s->linebuf, s->linebuf + i,
			    (size_t)s->linelen);
		s->linebuf[s->linelen] = '\0';
	}
}

/* Allocate a fresh gsession, register it, leave it detached (owner=NULL). */
static struct gsession *
gsession_new(struct iox_loop *loop)
{
	struct gsession *g;
	int i;

	g = calloc(1, sizeof(*g));
	if (!g)
		return NULL;
	g->loop = loop;
	g->pstate = PROC_NEVER;
	g->pid = 0;
	g->out_fd = g->err_fd = -1;
	g->ctl_fd = -1;
	g->owner = NULL;
	g->nokill = 0;
	g->name[0] = '\0';

	for (i = 0; i < N_STREAMS; i++) {
		if (log_buf_init(&g->logs[i], g_log_bytes) < 0) {
			while (--i >= 0)
				log_buf_free(&g->logs[i]);
			free(g);
			return NULL;
		}
	}
	if (gsession_register(g) < 0) {
		for (i = 0; i < N_STREAMS; i++)
			log_buf_free(&g->logs[i]);
		free(g);
		return NULL;
	}
	return g;
}

/* Tear down a gsession: kill+reap process, close pipes, free logs. */
static void
gsession_destroy(struct gsession *g)
{
	int i;

	if (!g)
		return;
	proc_reap_if_running(g);
	if (g->out_fd >= 0) {
		iox_fd_remove(g->loop, g->out_fd);
		close(g->out_fd);
		g->out_fd = -1;
	}
	if (g->err_fd >= 0) {
		iox_fd_remove(g->loop, g->err_fd);
		close(g->err_fd);
		g->err_fd = -1;
	}
	ctl_close(g);
	for (i = 0; i < N_STREAMS; i++)
		log_buf_free(&g->logs[i]);
	gsession_unregister(g);
	free(g);
}

static struct session *
session_new(struct iox_loop *loop, int fd)
{
	struct session *s;
	struct gsession *g;

	s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	g = gsession_new(loop);
	if (!g) {
		free(s);
		return NULL;
	}
	s->fd = fd;
	s->loop = loop;
	s->g = g;
	g->owner = s;

	if (iox_fd_add(loop, fd, IOX_READ, session_on_readable, s) < 0) {
		g->owner = NULL;
		gsession_destroy(g);
		free(s);
		return NULL;
	}
	return s;
}

static void
session_close(struct session *s)
{
	struct gsession *g;
	struct log_jq_job *j;

	if (!s)
		return;
	/* Orphan any in-flight log_jq jobs belonging to this session so
	 * their async callbacks stop writing to the dying fd.  The child
	 * itself gets killed; SIGCHLD will finalize and free the job. */
	for (j = g_jq_jobs; j; j = j->next) {
		if (j->s == s) {
			j->s = NULL;
			jq_kill_job(j);
		}
	}
	ctl_q_scrub_session(s);
	g = s->g;
	if (g) {
		g->owner = NULL;
		s->g = NULL;
		if (!g->nokill)
			gsession_destroy(g);
	}
	iox_fd_remove(s->loop, s->fd);
	close(s->fd);
	free(s->body_line);
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

	/* env vars override defaults */
	{
		const char *p = getenv("LUDICA_MCP_PORT");
		if (p && *p)
			port = atoi(p);
		p = getenv("LUDICA_MCP_LOG_BYTES");
		if (p && *p) {
			long v = atol(p);
			if (v > 0 && v < (long)INT_MAX)
				g_log_bytes = (size_t)v;
		}
	}

	signal(SIGPIPE, SIG_IGN);

	loop = iox_loop_new();
	if (!loop)
		die("iox_loop_new failed");

	iox_signal_add(loop, SIGINT, on_sigint, NULL);
	iox_signal_add(loop, SIGTERM, on_sigint, NULL);
	iox_signal_add(loop, SIGCHLD, on_sigchld, NULL);

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
