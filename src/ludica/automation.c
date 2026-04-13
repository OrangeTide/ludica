/*
 * automation.c — TCP automation server for ludica
 *
 * Non-blocking TCP server accepting one connection at a time.
 * Text protocol: one command per line (\n), one response per command.
 * No-ops when --auto-port is not set.
 */

#include "ludica_internal.h"
#include "ludica_auto.h"

#ifdef __EMSCRIPTEN__

void lud__auto_init(void) {}
void lud__auto_poll(void) {}
int  lud__auto_frame_allowed(void) { return 1; }
void lud__auto_post_frame(void) {}
void lud__auto_shutdown(void) {}
void lud_auto_register_int(const char *n, const int *p) { (void)n; (void)p; }
void lud_auto_register_str(const char *n, const char *const *p) { (void)n; (void)p; }

#else /* !__EMSCRIPTEN__ */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <strings.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
#define SOCK_WOULD_BLOCK (WSAGetLastError() == WSAEWOULDBLOCK)
#define strcasecmp _stricmp
static void sock_set_nonblock(sock_t s) {
	unsigned long mode = 1;
	ioctlsocket(s, FIONBIO, &mode);
}
static void sock_platform_init(void) {
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
}
static void sock_platform_shutdown(void) {
	WSACleanup();
}
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close close
#define SOCK_WOULD_BLOCK (errno == EAGAIN || errno == EWOULDBLOCK)
static void sock_set_nonblock(sock_t s) {
	fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
}
static void sock_platform_init(void) {}
static void sock_platform_shutdown(void) {}
#endif

#include <GLES2/gl2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ---- SIGUSR1 signal capture (POSIX only) ---- */

#ifndef _WIN32
#include <signal.h>
static volatile sig_atomic_t sigusr1_pending;
static void sigusr1_handler(int sig) { (void)sig; sigusr1_pending = 1; }
#endif

/* ---- Limits ---- */

#define LINE_BUF_SIZE  4096
#define RESP_BUF_SIZE  4096
#define MAX_TOKENS     16
#define MAX_VARS       64

/* ---- Variable registry ---- */

enum var_type { VAR_INT, VAR_STR };

struct auto_var {
	const char *name;
	enum var_type type;
	union {
		const int *ip;
		const char *const *sp;
	} u;
};

/* ---- Static state ---- */

static sock_t listen_fd = SOCK_INVALID;
static sock_t client_fd = SOCK_INVALID;
static char line_buf[LINE_BUF_SIZE];
static int line_len;

static FILE *replay_fp;
static int frame_delay;          /* file replay: frames to skip */

static int stepping;             /* frames remaining in STEP command */
static int wait_frame;           /* WAITEVENT FRAME pending */

static struct auto_var vars[MAX_VARS];
static int num_vars;

static char resp_buf[RESP_BUF_SIZE];

/* Tokenized command */
static char cmd_copy[LINE_BUF_SIZE];
static char *tokens[MAX_TOKENS];
static int num_tokens;

/* ---- Response helpers ---- */

static void
send_resp(const char *fmt, ...)
{
	va_list ap;
	int n;
	if (client_fd == SOCK_INVALID)
		return;
	va_start(ap, fmt);
	n = vsnprintf(resp_buf, sizeof(resp_buf) - 1, fmt, ap);
	va_end(ap);
	if (n > 0) {
		resp_buf[n] = '\n';
		send(client_fd, resp_buf, n + 1, 0);
	}
}

static void
resp_ok(const char *detail)
{
	if (detail && *detail)
		send_resp("OK %s", detail);
	else
		send_resp("OK");
}

static void
resp_err(const char *msg)
{
	send_resp("ERR %s", msg);
}

/* ---- Send all helper (for large responses) ---- */

static void
send_all(sock_t fd, const char *buf, size_t len)
{
	while (len > 0) {
		int n = send(fd, buf, (int)len, 0);
		if (n <= 0)
			break;
		buf += n;
		len -= (size_t)n;
	}
}

/* ---- Base64 encoding ---- */

static const char b64_table[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t
base64_encode(char *out, const unsigned char *in, size_t len)
{
	size_t i, j = 0;
	for (i = 0; i + 2 < len; i += 3) {
		out[j++] = b64_table[(in[i] >> 2) & 0x3f];
		out[j++] = b64_table[((in[i] & 0x03) << 4) | (in[i+1] >> 4)];
		out[j++] = b64_table[((in[i+1] & 0x0f) << 2) | (in[i+2] >> 6)];
		out[j++] = b64_table[in[i+2] & 0x3f];
	}
	if (i < len) {
		out[j++] = b64_table[(in[i] >> 2) & 0x3f];
		if (i + 1 < len) {
			out[j++] = b64_table[((in[i] & 0x03) << 4) | (in[i+1] >> 4)];
			out[j++] = b64_table[((in[i+1] & 0x0f) << 2)];
		} else {
			out[j++] = b64_table[(in[i] & 0x03) << 4];
			out[j++] = '=';
		}
		out[j++] = '=';
	}
	out[j] = '\0';
	return j;
}

/* ---- Screen capture ---- */

/* Memory writer for stbi_write_png_to_func */
struct mem_writer {
	unsigned char *buf;
	size_t len;
	size_t cap;
};

static void
mem_write_func(void *ctx, void *data, int size)
{
	struct mem_writer *w = ctx;
	if (w->len + (size_t)size > w->cap) {
		size_t newcap = (w->cap == 0) ? 4096 : w->cap;
		unsigned char *nb;
		while (newcap < w->len + (size_t)size)
			newcap *= 2;
		nb = realloc(w->buf, newcap);
		if (!nb) {
			w->cap = w->len = 0;
			return;
		}
		w->buf = nb;
		w->cap = newcap;
	}
	memcpy(w->buf + w->len, data, (size_t)size);
	w->len += (size_t)size;
}

/* Read pixels from GL framebuffer, flip vertically, return malloc'd RGBA */
static unsigned char *
capture_pixels(int x, int y, int w, int h)
{
	unsigned char *pixels, *temp_row, *top, *bot;
	int stride, row;

	stride = w * 4;
	pixels = malloc((size_t)(stride * h));
	temp_row = malloc((size_t)stride);
	if (!pixels || !temp_row) {
		free(pixels);
		free(temp_row);
		return NULL;
	}

	glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	/* flip vertically (GL origin is bottom-left) */
	for (row = 0; row < h / 2; row++) {
		top = pixels + row * stride;
		bot = pixels + (h - 1 - row) * stride;
		memcpy(temp_row, top, (size_t)stride);
		memcpy(top, bot, (size_t)stride);
		memcpy(bot, temp_row, (size_t)stride);
	}
	free(temp_row);
	return pixels;
}

static void
build_capture_path(char *out, size_t outsz, const char *name)
{
	const char *dir = lud__state.capture_dir ? lud__state.capture_dir : ".";
	if (name && *name)
		snprintf(out, outsz, "%s/%s", dir, name);
	else
		snprintf(out, outsz, "%s/frame_%06llu.png", dir,
		         lud__state.frame_count);
}

/* Capture a rectangle and either write to file or send base64 over TCP */
static void
send_capture(int x, int y, int w, int h, int use_base64, const char *filename)
{
	unsigned char *pixels = capture_pixels(x, y, w, h);
	if (!pixels) {
		resp_err("capture failed");
		return;
	}

	if (use_base64) {
		struct mem_writer mw = {NULL, 0, 0};
		size_t b64len, total;
		char *b64;
		int prefix;

		stbi_write_png_to_func(mem_write_func, &mw, w, h, 4, pixels,
		                       w * 4);
		free(pixels);
		if (!mw.buf) {
			resp_err("PNG encode failed");
			return;
		}

		b64len = ((mw.len + 2) / 3) * 4;
		b64 = malloc(b64len + 32);
		if (!b64) {
			free(mw.buf);
			resp_err("out of memory");
			return;
		}

		prefix = sprintf(b64, "OK base64:");
		base64_encode(b64 + prefix, mw.buf, mw.len);
		free(mw.buf);

		total = (size_t)prefix + b64len;
		b64[total] = '\n';
		if (client_fd != SOCK_INVALID)
			send_all(client_fd, b64, total + 1);
		free(b64);
	} else {
		char path[512];
		build_capture_path(path, sizeof(path), filename);
		if (!stbi_write_png(path, w, h, 4, pixels, w * 4)) {
			free(pixels);
			resp_err("write failed");
			return;
		}
		free(pixels);
		send_resp("OK %s", path);
	}
}

/* Capture full screen to file (used by SIGUSR1) */
static void
capture_screen_to_file(void)
{
	char path[512];
	unsigned char *pixels;
	int w = lud__state.win_width;
	int h = lud__state.win_height;

	build_capture_path(path, sizeof(path), NULL);
	pixels = capture_pixels(0, 0, w, h);
	if (!pixels)
		return;
	if (stbi_write_png(path, w, h, 4, pixels, w * 4))
		lud_log("automation: captured %s", path);
	free(pixels);
}

/* ---- Tokenizer ---- */

static int
tokenize(const char *line)
{
	size_t len = strlen(line);
	if (len >= sizeof(cmd_copy))
		len = sizeof(cmd_copy) - 1;
	memcpy(cmd_copy, line, len);
	cmd_copy[len] = '\0';

	num_tokens = 0;
	char *p = cmd_copy;
	while (*p && num_tokens < MAX_TOKENS) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		if (!*p)
			break;
		tokens[num_tokens++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			p++;
		if (*p)
			*p++ = '\0';
	}
	return num_tokens;
}

/* ---- Command handlers ---- */

static void
cmd_keydown(void)
{
	lud_event_t ev;
	enum lud_keycode kc;
	if (num_tokens < 2) { resp_err("usage: KEYDOWN <key>"); return; }
	kc = lud_key_from_name(tokens[1]);
	if (kc == LUD_KEY_UNKNOWN) { resp_err("unknown key"); return; }
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_KEY_DOWN;
	ev.key.keycode = kc;
	lud__event_push(&ev);
	resp_ok(NULL);
}

static void
cmd_keyup(void)
{
	lud_event_t ev;
	enum lud_keycode kc;
	if (num_tokens < 2) { resp_err("usage: KEYUP <key>"); return; }
	kc = lud_key_from_name(tokens[1]);
	if (kc == LUD_KEY_UNKNOWN) { resp_err("unknown key"); return; }
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_KEY_UP;
	ev.key.keycode = kc;
	lud__event_push(&ev);
	resp_ok(NULL);
}

static void
cmd_mousemove(void)
{
	lud_event_t ev;
	if (num_tokens < 3) { resp_err("usage: MOUSEMOVE <x> <y>"); return; }
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_MOUSE_MOVE;
	ev.mouse_move.x = atoi(tokens[1]);
	ev.mouse_move.y = atoi(tokens[2]);
	lud__event_push(&ev);
	resp_ok(NULL);
}

static void
cmd_mousedown(void)
{
	lud_event_t ev;
	if (num_tokens < 4) { resp_err("usage: MOUSEDOWN <btn> <x> <y>"); return; }
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_MOUSE_DOWN;
	ev.mouse_button.button = atoi(tokens[1]);
	ev.mouse_button.x = atoi(tokens[2]);
	ev.mouse_button.y = atoi(tokens[3]);
	lud__event_push(&ev);
	resp_ok(NULL);
}

static void
cmd_mouseup(void)
{
	lud_event_t ev;
	if (num_tokens < 4) { resp_err("usage: MOUSEUP <btn> <x> <y>"); return; }
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_MOUSE_UP;
	ev.mouse_button.button = atoi(tokens[1]);
	ev.mouse_button.x = atoi(tokens[2]);
	ev.mouse_button.y = atoi(tokens[3]);
	lud__event_push(&ev);
	resp_ok(NULL);
}

static void
cmd_scroll(void)
{
	lud_event_t ev;
	if (num_tokens < 3) { resp_err("usage: SCROLL <dx> <dy>"); return; }
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_MOUSE_SCROLL;
	ev.scroll.dx = (float)atof(tokens[1]);
	ev.scroll.dy = (float)atof(tokens[2]);
	lud__event_push(&ev);
	resp_ok(NULL);
}

static void
cmd_listactions(void)
{
	int n = lud__action_count();
	char *p = resp_buf;
	char *end = resp_buf + sizeof(resp_buf) - 2;
	int i, k;

	p += snprintf(p, end - p, "OK");
	for (i = 0; i < n && p < end; i++) {
		const char *name = lud__action_name(i);
		int nk = lud__action_key_count(i);
		p += snprintf(p, end - p, " %s=", name);
		if (nk == 0) {
			p += snprintf(p, end - p, "(none)");
		} else {
			for (k = 0; k < nk && p < end; k++) {
				const char *kn = lud__key_name(lud__action_key(i, k));
				if (k > 0 && p < end)
					*p++ = ',';
				if (kn)
					p += snprintf(p, end - p, "%s", kn);
			}
		}
	}
	*p++ = '\n';
	*p = '\0';
	if (client_fd != SOCK_INVALID)
		send(client_fd, resp_buf, p - resp_buf, 0);
}

static void
cmd_action(void)
{
	int mode;
	if (num_tokens < 2) { resp_err("usage: ACTION <name> [HOLD|RELEASE]"); return; }

	mode = 0; /* default: press + auto-release */
	if (num_tokens >= 3) {
		if (strcasecmp(tokens[2], "HOLD") == 0)
			mode = 1;
		else if (strcasecmp(tokens[2], "RELEASE") == 0)
			mode = 2;
	}

	if (lud__action_inject(tokens[1], mode) != 0) {
		resp_err("unknown action");
		return;
	}
	resp_ok(NULL);
}

static int
cmd_step(void)
{
	int n = (num_tokens > 1) ? atoi(tokens[1]) : 1;
	if (n < 1) n = 1;
	lud__state.paused = 1;
	stepping = n;
	/* response deferred until stepping completes */
	return 1; /* blocking */
}

static int
cmd_framedelay(void)
{
	int n;
	if (num_tokens < 2) { resp_err("usage: FRAMEDELAY <count>"); return 0; }
	n = atoi(tokens[1]);
	if (n < 1) { resp_err("count must be >= 1"); return 0; }
	frame_delay = n;
	resp_ok(NULL);
	return 1; /* blocking */
}

static int
cmd_waitevent(void)
{
	if (num_tokens < 2) { resp_err("usage: WAITEVENT FRAME"); return 0; }
	if (strcasecmp(tokens[1], "FRAME") == 0) {
		wait_frame = 1;
		/* response deferred until frame completes */
		return 1; /* blocking */
	}
	resp_err("unsupported event type (only FRAME supported)");
	return 0;
}

static void
cmd_seed(void)
{
	unsigned int s;
	if (num_tokens < 2) { resp_err("usage: SEED <n>"); return; }
	s = (unsigned int)strtoul(tokens[1], NULL, 10);
	srand(s);
	resp_ok(NULL);
}

static void
cmd_query(void)
{
	if (num_tokens < 2) { resp_err("usage: QUERY FRAME|SIZE|FPS|VAR <name>"); return; }

	if (strcasecmp(tokens[1], "FRAME") == 0) {
		send_resp("OK %llu", lud__state.frame_count);
	} else if (strcasecmp(tokens[1], "SIZE") == 0) {
		send_resp("OK %d %d", lud__state.win_width, lud__state.win_height);
	} else if (strcasecmp(tokens[1], "FPS") == 0) {
		float fps = (lud__state.frame_dt > 0.0f) ? 1.0f / lud__state.frame_dt : 0.0f;
		send_resp("OK %.1f", fps);
	} else if (strcasecmp(tokens[1], "VAR") == 0) {
		int i;
		if (num_tokens < 3) { resp_err("usage: QUERY VAR <name>"); return; }
		for (i = 0; i < num_vars; i++) {
			if (strcmp(vars[i].name, tokens[2]) == 0) {
				if (vars[i].type == VAR_INT)
					send_resp("OK %d", *vars[i].u.ip);
				else
					send_resp("OK %s", *vars[i].u.sp ? *vars[i].u.sp : "");
				return;
			}
		}
		resp_err("unknown variable");
	} else {
		resp_err("unknown query");
	}
}

static void
cmd_listvar(void)
{
	char *p = resp_buf;
	char *end = resp_buf + sizeof(resp_buf) - 2;
	int i;

	p += snprintf(p, end - p, "OK");
	for (i = 0; i < num_vars && p < end; i++)
		p += snprintf(p, end - p, " %s", vars[i].name);
	*p++ = '\n';
	*p = '\0';
	if (client_fd != SOCK_INVALID)
		send(client_fd, resp_buf, p - resp_buf, 0);
}

static void
cmd_quit(void)
{
	resp_ok(NULL);
	lud_quit();
}

static void
cmd_resume(void)
{
	lud__state.paused = 0;
	resp_ok(NULL);
}

/* ---- Capture commands ---- */

static void
cmd_capscreen(void)
{
	int use_base64 = 0, i;
	const char *filename = NULL;

	for (i = 1; i < num_tokens; i++) {
		if (strcasecmp(tokens[i], "--base64") == 0)
			use_base64 = 1;
		else
			filename = tokens[i];
	}
	send_capture(0, 0, lud__state.win_width, lud__state.win_height,
	             use_base64, filename);
}

static void
cmd_caprect(void)
{
	int x, y, w, h, gl_y, use_base64 = 0, i;
	const char *filename = NULL;

	if (num_tokens < 5) {
		resp_err("usage: CAPRECT <x> <y> <w> <h> [--base64] [filename]");
		return;
	}
	x = atoi(tokens[1]);
	y = atoi(tokens[2]);
	w = atoi(tokens[3]);
	h = atoi(tokens[4]);

	for (i = 5; i < num_tokens; i++) {
		if (strcasecmp(tokens[i], "--base64") == 0)
			use_base64 = 1;
		else
			filename = tokens[i];
	}

	/* Convert from top-left origin to GL bottom-left origin */
	gl_y = lud__state.win_height - y - h;
	send_capture(x, gl_y, w, h, use_base64, filename);
}

static void
cmd_readpixel(void)
{
	unsigned char pixel[4];
	int x, y, gl_y;

	if (num_tokens < 3) {
		resp_err("usage: READPIXEL <x> <y>");
		return;
	}
	x = atoi(tokens[1]);
	y = atoi(tokens[2]);
	gl_y = lud__state.win_height - 1 - y;
	glReadPixels(x, gl_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	send_resp("OK %d %d %d", pixel[0], pixel[1], pixel[2]);
}

/* ---- Command dispatch ---- */

/* Returns 1 if the command is blocking (STEP, FRAMEDELAY, WAITEVENT) */
static int
process_command(const char *line)
{
	if (tokenize(line) == 0)
		return 0;

	if (strcasecmp(tokens[0], "KEYDOWN") == 0)       cmd_keydown();
	else if (strcasecmp(tokens[0], "KEYUP") == 0)     cmd_keyup();
	else if (strcasecmp(tokens[0], "MOUSEMOVE") == 0) cmd_mousemove();
	else if (strcasecmp(tokens[0], "MOUSEDOWN") == 0) cmd_mousedown();
	else if (strcasecmp(tokens[0], "MOUSEUP") == 0)   cmd_mouseup();
	else if (strcasecmp(tokens[0], "SCROLL") == 0)    cmd_scroll();
	else if (strcasecmp(tokens[0], "LISTACTIONS") == 0) cmd_listactions();
	else if (strcasecmp(tokens[0], "ACTION") == 0)    cmd_action();
	else if (strcasecmp(tokens[0], "STEP") == 0)      return cmd_step();
	else if (strcasecmp(tokens[0], "FRAMEDELAY") == 0) return cmd_framedelay();
	else if (strcasecmp(tokens[0], "WAITEVENT") == 0) return cmd_waitevent();
	else if (strcasecmp(tokens[0], "SEED") == 0)      cmd_seed();
	else if (strcasecmp(tokens[0], "QUERY") == 0)     cmd_query();
	else if (strcasecmp(tokens[0], "LISTVAR") == 0)   cmd_listvar();
	else if (strcasecmp(tokens[0], "QUIT") == 0)      cmd_quit();
	else if (strcasecmp(tokens[0], "RESUME") == 0)    cmd_resume();
	else if (strcasecmp(tokens[0], "CAPSCREEN") == 0) cmd_capscreen();
	else if (strcasecmp(tokens[0], "CAPRECT") == 0)   cmd_caprect();
	else if (strcasecmp(tokens[0], "READPIXEL") == 0) cmd_readpixel();
	else resp_err("unknown command");

	return 0;
}

/* ---- TCP server management ---- */

static int
start_listener(int port)
{
	struct sockaddr_in addr;
	int opt = 1;

	sock_platform_init();

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == SOCK_INVALID) {
		lud_err("automation: socket() failed");
		return -1;
	}

	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
	           (const char *)&opt, sizeof(opt));
	sock_set_nonblock(listen_fd);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((unsigned short)port);

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		lud_err("automation: bind(%d) failed", port);
		sock_close(listen_fd);
		listen_fd = SOCK_INVALID;
		return -1;
	}

	if (listen(listen_fd, 1) < 0) {
		lud_err("automation: listen() failed");
		sock_close(listen_fd);
		listen_fd = SOCK_INVALID;
		return -1;
	}

	lud_log("automation: listening on 127.0.0.1:%d", port);
	return 0;
}

static void
try_accept(void)
{
	sock_t fd;
	if (listen_fd == SOCK_INVALID || client_fd != SOCK_INVALID)
		return;

	fd = accept(listen_fd, NULL, NULL);
	if (fd == SOCK_INVALID)
		return;

	sock_set_nonblock(fd);
	client_fd = fd;
	line_len = 0;
	lud_log("automation: client connected");
}

static void
close_client(void)
{
	if (client_fd != SOCK_INVALID) {
		sock_close(client_fd);
		client_fd = SOCK_INVALID;
		line_len = 0;
		stepping = 0;
		wait_frame = 0;
		lud_log("automation: client disconnected");
	}
}

static void
read_and_dispatch(void)
{
	char tmp[1024];
	int n, i;

	if (client_fd == SOCK_INVALID)
		return;

	/* don't read commands while stepping or waiting */
	if (stepping > 0 || wait_frame)
		return;

	n = recv(client_fd, tmp, sizeof(tmp), 0);
	if (n <= 0) {
		if (n == 0 || !SOCK_WOULD_BLOCK) {
			close_client();
		}
		return;
	}

	for (i = 0; i < n; i++) {
		if (tmp[i] == '\n') {
			line_buf[line_len] = '\0';
			if (line_len > 0)
				process_command(line_buf);
			line_len = 0;
			/* if command was blocking, stop processing */
			if (stepping > 0 || wait_frame)
				return;
		} else if (line_len < LINE_BUF_SIZE - 1) {
			line_buf[line_len++] = tmp[i];
		}
	}
}

/* ---- File replay ---- */

static void
replay_poll(void)
{
	char line[LINE_BUF_SIZE];

	if (!replay_fp)
		return;

	/* waiting for frame delay to expire */
	if (frame_delay > 0) {
		frame_delay--;
		return;
	}

	/* process commands until blocking or EOF */
	while (fgets(line, sizeof(line), replay_fp)) {
		/* strip trailing whitespace */
		size_t len = strlen(line);
		while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'
		       || line[len-1] == ' '))
			line[--len] = '\0';

		/* skip empty lines and comments */
		if (len == 0 || line[0] == '#')
			continue;

		if (process_command(line))
			return; /* blocking command */
	}

	/* EOF */
	fclose(replay_fp);
	replay_fp = NULL;
	lud_log("automation: replay file finished");
}

/* ---- Public entry points ---- */

void
lud__auto_init(void)
{
	if (lud__state.auto_port > 0) {
		start_listener(lud__state.auto_port);
	}

	if (lud__state.auto_file) {
		replay_fp = fopen(lud__state.auto_file, "r");
		if (!replay_fp)
			lud_err("automation: cannot open replay file '%s'",
			        lud__state.auto_file);
		else
			lud_log("automation: replaying from '%s'",
			        lud__state.auto_file);
	}

	if (lud__state.paused && lud__state.auto_port == 0 && !replay_fp)
		lud_log("automation: --paused without --auto-port or --auto-file, "
		        "game will be frozen");

#ifndef _WIN32
	{
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = sigusr1_handler;
		sigaction(SIGUSR1, &sa, NULL);
	}
#endif
}

void
lud__auto_poll(void)
{
	if (listen_fd != SOCK_INVALID) {
		try_accept();
		read_and_dispatch();
	}
	replay_poll();
}

int
lud__auto_frame_allowed(void)
{
	if (!lud__state.paused)
		return 1;
	if (stepping > 0)
		return 1;
	return 0;
}

void
lud__auto_post_frame(void)
{
	/* handle STEP completion */
	if (stepping > 0) {
		stepping--;
		if (stepping == 0) {
			send_resp("OK %llu", lud__state.frame_count);
		}
	}

	/* handle WAITEVENT FRAME */
	if (wait_frame) {
		wait_frame = 0;
		send_resp("OK %llu", lud__state.frame_count);
	}

	/* auto-release actions that were pressed via ACTION command */
	lud__action_auto_release();

#ifndef _WIN32
	/* SIGUSR1 capture */
	if (sigusr1_pending) {
		sigusr1_pending = 0;
		capture_screen_to_file();
	}
#endif
}

void
lud__auto_shutdown(void)
{
	close_client();
	if (listen_fd != SOCK_INVALID) {
		sock_close(listen_fd);
		listen_fd = SOCK_INVALID;
	}
	if (replay_fp) {
		fclose(replay_fp);
		replay_fp = NULL;
	}
	sock_platform_shutdown();
}

/* ---- Public API: variable registration ---- */

void
lud_auto_register_int(const char *name, const int *ptr)
{
	if (num_vars >= MAX_VARS)
		return;
	vars[num_vars].name = name;
	vars[num_vars].type = VAR_INT;
	vars[num_vars].u.ip = ptr;
	num_vars++;
}

void
lud_auto_register_str(const char *name, const char *const *ptr)
{
	if (num_vars >= MAX_VARS)
		return;
	vars[num_vars].name = name;
	vars[num_vars].type = VAR_STR;
	vars[num_vars].u.sp = ptr;
	num_vars++;
}

#endif /* !__EMSCRIPTEN__ */
