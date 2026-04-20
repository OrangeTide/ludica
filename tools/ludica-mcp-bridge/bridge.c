/*
 * ludica-mcp-bridge -- stdio MCP JSON-RPC to TCP launcher translator.
 *
 * Pure translation layer. One process per Claude session. No state.
 *
 * Reads newline-delimited JSON-RPC from stdin; writes responses to
 * stdout. Connects to the ludica launcher on 127.0.0.1:LUDICA_MCP_PORT
 * (default 4000). If the launcher is not running, every tool call
 * reports an error and the agent is expected to ask the user to start
 * the launcher.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define JSMN_STATIC
#include "jsmn.h"

#define LINE_MAX_SZ   (256 * 1024)
#define RESP_MAX_SZ   (4 * 1024 * 1024)  /* screenshots can be big */
#define TOK_MAX       1024
#define TCP_BUF_SZ    (4 * 1024 * 1024)

static int game_fd = -1;
static int game_port = 4000;
static char line_buf[LINE_MAX_SZ];
static char *resp_buf;
static char *tcp_buf;
static jsmntok_t tokens[TOK_MAX];

/* ---- stderr logging ---- */

static void
logmsg(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "ludica-mcp-bridge: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

/* ---- JSON helpers (subset of jsmn idioms) ---- */

static int
tok_eq(const char *json, const jsmntok_t *t, const char *s)
{
	int len = t->end - t->start;
	return (t->type == JSMN_STRING &&
	        (int)strlen(s) == len &&
	        strncmp(json + t->start, s, (size_t)len) == 0);
}

static void
tok_str(const char *json, const jsmntok_t *t, char *out, size_t outsz)
{
	int len = t->end - t->start;
	if ((size_t)len >= outsz)
		len = (int)outsz - 1;
	memcpy(out, json + t->start, (size_t)len);
	out[len] = '\0';
}

static int
tok_int(const char *json, const jsmntok_t *t)
{
	char buf[32];
	tok_str(json, t, buf, sizeof(buf));
	return atoi(buf);
}

static int
tok_skip(const jsmntok_t *t, int ntok, int idx)
{
	int i, j;
	if (idx >= ntok)
		return idx;
	if (t[idx].type == JSMN_OBJECT) {
		j = idx + 1;
		for (i = 0; i < t[idx].size; i++) {
			j = tok_skip(t, ntok, j);
			j = tok_skip(t, ntok, j);
		}
		return j;
	}
	if (t[idx].type == JSMN_ARRAY) {
		j = idx + 1;
		for (i = 0; i < t[idx].size; i++)
			j = tok_skip(t, ntok, j);
		return j;
	}
	return idx + 1;
}

static int
json_obj_find(const char *json, const jsmntok_t *t, int ntok,
              int obj_idx, const char *key)
{
	int i, j;
	if (t[obj_idx].type != JSMN_OBJECT)
		return -1;
	j = obj_idx + 1;
	for (i = 0; i < t[obj_idx].size; i++) {
		if (j + 1 >= ntok)
			break;
		if (tok_eq(json, &t[j], key))
			return j + 1;
		j = tok_skip(t, ntok, j);
		j = tok_skip(t, ntok, j);
	}
	return -1;
}

static int
get_arg_str(const char *json, const jsmntok_t *t, int ntok,
            int args_idx, const char *key, char *out, size_t outsz)
{
	int vi;
	if (args_idx < 0) return -1;
	vi = json_obj_find(json, t, ntok, args_idx, key);
	if (vi < 0) return -1;
	tok_str(json, &t[vi], out, outsz);
	return 0;
}

static int
get_arg_int(const char *json, const jsmntok_t *t, int ntok,
            int args_idx, const char *key, int *out)
{
	int vi;
	if (args_idx < 0) return -1;
	vi = json_obj_find(json, t, ntok, args_idx, key);
	if (vi < 0) return -1;
	*out = tok_int(json, &t[vi]);
	return 0;
}

static int
get_arg_bool(const char *json, const jsmntok_t *t, int ntok,
             int args_idx, const char *key, int *out)
{
	int vi;
	if (args_idx < 0) return -1;
	vi = json_obj_find(json, t, ntok, args_idx, key);
	if (vi < 0) return -1;
	if (t[vi].type == JSMN_PRIMITIVE) {
		char c = json[t[vi].start];
		*out = (c == 't');
		return 0;
	}
	return -1;
}

/* Iterate through a string array at args_idx/key, appending each element
 * to dst as " elem". Returns number of elements consumed, or 0. */
static int
append_str_array(const char *json, const jsmntok_t *t, int ntok,
                 int args_idx, const char *key, char *dst, size_t dstsz)
{
	int vi, i, n;
	size_t len;

	if (args_idx < 0)
		return 0;
	vi = json_obj_find(json, t, ntok, args_idx, key);
	if (vi < 0 || t[vi].type != JSMN_ARRAY)
		return 0;

	n = 0;
	for (i = 0; i < t[vi].size; i++) {
		int ei = vi + 1 + i;  /* flat for strings */
		if (ei >= ntok)
			break;
		len = strlen(dst);
		if (len + 2 >= dstsz)
			break;
		dst[len] = ' ';
		dst[len + 1] = '\0';
		tok_str(json, &t[ei], dst + len + 1, dstsz - len - 1);
		n++;
	}
	return n;
}

/* ---- JSON output ---- */

static int
json_escape(char *out, size_t outsz, const char *s, size_t slen)
{
	size_t i = 0, j = 0;
	while (j < slen && i + 7 < outsz) {
		unsigned char c = (unsigned char)s[j++];
		switch (c) {
		case '"':  out[i++] = '\\'; out[i++] = '"'; break;
		case '\\': out[i++] = '\\'; out[i++] = '\\'; break;
		case '\n': out[i++] = '\\'; out[i++] = 'n'; break;
		case '\r': out[i++] = '\\'; out[i++] = 'r'; break;
		case '\t': out[i++] = '\\'; out[i++] = 't'; break;
		default:
			if (c < 0x20) {
				i += (size_t)snprintf(out + i, outsz - i,
				                      "\\u%04x", c);
			} else {
				out[i++] = (char)c;
			}
			break;
		}
	}
	out[i] = '\0';
	return (int)i;
}

static void
send_response(const char *json)
{
	fputs(json, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

/* ---- TCP ---- */

static int
tcp_connect(void)
{
	struct sockaddr_in addr;

	if (game_fd >= 0)
		return 0;
	game_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (game_fd < 0) {
		logmsg("socket: %s", strerror(errno));
		return -1;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((unsigned short)game_port);
	if (connect(game_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		logmsg("connect 127.0.0.1:%d: %s", game_port, strerror(errno));
		close(game_fd);
		game_fd = -1;
		return -1;
	}
	return 0;
}

static void
tcp_drop(void)
{
	if (game_fd >= 0) {
		close(game_fd);
		game_fd = -1;
	}
}

/* Send a command (adds trailing newline if missing) and read a response.
 *
 * Response model: every command returns at least one line starting with
 * "OK" or "ERR". Some commands (log_tail, log_head, log_range, log_grep,
 * help) follow the status line with additional data lines. There is no
 * Multi-line framing: body lines follow the `OK\n` status line, and the
 * body is terminated by a sentinel line at column 0:
 *   `END\n`               -- success
 *   `END ERR <reason>\n`  -- async failure
 * Body lines that begin with `\` or with the three bytes `END` are
 * escape-encoded by the launcher with one extra leading `\`; we strip
 * one leading `\` from every body line.  A `END ERR ...` terminator is
 * rewritten into a plain `ERR <reason>\n` so downstream code can treat
 * it like any other error.
 *
 * Returns total byte length written into tcp_buf (NUL-terminated), or
 * -1 on I/O failure.
 */
static int
tcp_command(const char *cmd, int multiline_ok)
{
	int len, n, total;
	char *nl;

	if (tcp_connect() < 0)
		return -1;

	len = (int)strlen(cmd);
	if (send(game_fd, cmd, len, 0) != len)
		goto fail;
	if (len == 0 || cmd[len - 1] != '\n') {
		if (send(game_fd, "\n", 1, 0) != 1)
			goto fail;
	}

	total = 0;
	/* Block until we have at least one complete status line. */
	for (;;) {
		n = recv(game_fd, tcp_buf + total, TCP_BUF_SZ - 1 - total, 0);
		if (n <= 0)
			goto fail;
		total += n;
		tcp_buf[total] = '\0';
		nl = memchr(tcp_buf, '\n', (size_t)total);
		if (nl)
			break;
		if (total >= TCP_BUF_SZ - 1)
			goto fail;
	}

	/* If caller expects a single-line response (or status line is ERR),
	 * stop now even if more might come — don't stall. */
	if (!multiline_ok || strncmp(tcp_buf, "ERR", 3) == 0)
		return total;

	/* Multi-line body: read until a line starting with "END" (followed
	 * by '\n' or ' ') is seen at a line boundary. */
	{
		size_t body_start = (size_t)(nl - tcp_buf) + 1;
		size_t scan_from = body_start;
		size_t term_start = 0, term_end = 0;
		int found = 0;

		for (;;) {
			while (scan_from < (size_t)total) {
				char *lnl = memchr(tcp_buf + scan_from, '\n',
				    (size_t)total - scan_from);
				if (!lnl)
					break;
				size_t llen = (size_t)(lnl - (tcp_buf
				    + scan_from));
				if (llen >= 3 &&
				    tcp_buf[scan_from] == 'E' &&
				    tcp_buf[scan_from + 1] == 'N' &&
				    tcp_buf[scan_from + 2] == 'D' &&
				    (llen == 3 ||
				     tcp_buf[scan_from + 3] == ' ')) {
					term_start = scan_from;
					term_end = (size_t)(lnl - tcp_buf) + 1;
					found = 1;
					break;
				}
				scan_from = (size_t)(lnl - tcp_buf) + 1;
			}
			if (found)
				break;
			if (total >= TCP_BUF_SZ - 1)
				goto fail;
			n = recv(game_fd, tcp_buf + total,
			    TCP_BUF_SZ - 1 - total, 0);
			if (n <= 0)
				goto fail;
			total += n;
			tcp_buf[total] = '\0';
		}

		/* Check for error terminator: "END ERR <reason>\n". */
		if (term_end - term_start >= 8 &&
		    memcmp(tcp_buf + term_start, "END ERR ", 8) == 0) {
			size_t rlen = term_end - term_start - 4; /* drop "END " */
			memmove(tcp_buf, tcp_buf + term_start + 4, rlen);
			total = (int)rlen;
			tcp_buf[total] = '\0';
			return total;
		}

		/* Success: strip terminator and unescape body lines in place.
		 * For each body line, if its first char is '\\', drop it. */
		{
			size_t src = body_start;
			size_t dst = body_start;
			int at_line_start = 1;
			while (src < term_start) {
				char c = tcp_buf[src];
				if (at_line_start && c == '\\') {
					src++;
					at_line_start = 0;
					continue;
				}
				tcp_buf[dst++] = c;
				at_line_start = (c == '\n');
				src++;
			}
			total = (int)dst;
			tcp_buf[total] = '\0';
		}
	}
	return total;

fail:
	tcp_drop();
	return -1;
}

/* ---- Response builders ---- */

static void
result_text(const char *id_raw, const char *text, size_t text_len,
            int is_error)
{
	/* Escape into a temporary, then wrap in the JSON-RPC envelope.
	 * Reserve 256 bytes of the response buffer for the envelope. */
	static char escaped[RESP_MAX_SZ - 256];
	json_escape(escaped, sizeof(escaped), text, text_len);
	snprintf(resp_buf, RESP_MAX_SZ,
		"{\"jsonrpc\":\"2.0\",\"id\":%s,"
		"\"result\":{\"content\":[{\"type\":\"text\","
		"\"text\":\"%s\"}],\"isError\":%s}}",
		id_raw, escaped, is_error ? "true" : "false");
	send_response(resp_buf);
}

static void
result_image(const char *id_raw, const char *b64, size_t b64_len)
{
	/* base64 needs no escaping, but we still cap length via the buffer. */
	int n;
	n = snprintf(resp_buf, RESP_MAX_SZ,
		"{\"jsonrpc\":\"2.0\",\"id\":%s,"
		"\"result\":{\"content\":[{\"type\":\"image\","
		"\"data\":\"",
		id_raw);
	if (n < 0 || (size_t)n + b64_len + 64 >= RESP_MAX_SZ) {
		result_text(id_raw, "image too large", 15, 1);
		return;
	}
	memcpy(resp_buf + n, b64, b64_len);
	n += (int)b64_len;
	n += snprintf(resp_buf + n, RESP_MAX_SZ - n,
		"\",\"mimeType\":\"image/png\"}],\"isError\":false}}");
	resp_buf[n] = '\0';
	send_response(resp_buf);
}

static void
result_error(const char *id_raw, int code, const char *msg)
{
	char escaped[512];
	json_escape(escaped, sizeof(escaped), msg, strlen(msg));
	snprintf(resp_buf, RESP_MAX_SZ,
		"{\"jsonrpc\":\"2.0\",\"id\":%s,"
		"\"error\":{\"code\":%d,\"message\":\"%s\"}}",
		id_raw, code, escaped);
	send_response(resp_buf);
}

/* Dispatch a command, format its response as MCP text or image content.
 * If `multiline` is set, we read multi-line responses.
 * If `image_prefix` is non-NULL and reply begins "OK <image_prefix>",
 * the rest is returned as image content. */
static void
dispatch_cmd(const char *id_raw, const char *cmd, int multiline,
             const char *image_prefix)
{
	int total;

	total = tcp_command(cmd, multiline);
	if (total < 0) {
		result_text(id_raw, "not connected to launcher", 25, 1);
		return;
	}

	/* status line = tcp_buf up to the first '\n'. The whole thing
	 * (including the status line) is our "body". */
	if (strncmp(tcp_buf, "ERR", 3) == 0) {
		/* strip trailing newline for display */
		size_t n = (size_t)total;
		while (n > 0 && (tcp_buf[n - 1] == '\n' ||
		       tcp_buf[n - 1] == '\r'))
			n--;
		result_text(id_raw, tcp_buf, n, 1);
		return;
	}
	if (strncmp(tcp_buf, "OK", 2) != 0) {
		result_text(id_raw, tcp_buf, (size_t)total, 1);
		return;
	}

	/* Image mode: look for "OK <prefix>" on the first line. */
	if (image_prefix) {
		size_t pfxlen = strlen(image_prefix);
		char *nl = memchr(tcp_buf, '\n', (size_t)total);
		if (nl &&
		    (size_t)total > 3 + pfxlen &&
		    strncmp(tcp_buf + 3, image_prefix, pfxlen) == 0) {
			char *data = tcp_buf + 3 + pfxlen;
			size_t dlen = (size_t)(nl - data);
			result_image(id_raw, data, dlen);
			return;
		}
	}

	/* Success: drop "OK" / "OK " / "OK\n" prefix, return rest as text. */
	{
		size_t off = 2;
		size_t n;
		if (off < (size_t)total && tcp_buf[off] == ' ')
			off++;
		else if (off < (size_t)total && tcp_buf[off] == '\n')
			off++;
		n = (size_t)total - off;
		/* strip trailing \r/\n for tidy display */
		while (n > 0 && (tcp_buf[off + n - 1] == '\n' ||
		       tcp_buf[off + n - 1] == '\r'))
			n--;
		if (n == 0)
			result_text(id_raw, "OK", 2, 0);
		else
			result_text(id_raw, tcp_buf + off, n, 0);
	}
}

/* ---- Tool schema ---- */

/* clang-format off */
static const char *tools_json =
"["
/* lifecycle */
"{\"name\":\"spawn\",\"description\":\"Spawn a game process. alias matches an entry in LUDICA_MCP_ALLOWEXEC; extra args are passed through.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"alias\":{\"type\":\"string\"},"
  "\"args\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},"
  "\"required\":[\"alias\"]}},"

"{\"name\":\"kill\",\"description\":\"Kill the running game (default SIGTERM).\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"signal\":{\"type\":\"string\",\"description\":\"TERM, KILL, INT, etc.\"}}}},"

"{\"name\":\"status\",\"description\":\"Query the game's process state.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"env\",\"description\":\"Read or set an env var for the next spawn.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"key\":{\"type\":\"string\"},"
  "\"value\":{\"type\":\"string\"}},"
  "\"required\":[\"key\"]}},"

"{\"name\":\"unsetenv\",\"description\":\"Unset an env var.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"key\":{\"type\":\"string\"}},\"required\":[\"key\"]}},"

/* logs */
"{\"name\":\"log_tail\",\"description\":\"Emit last N log lines.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"n\":{\"type\":\"integer\"},"
  "\"streams\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"stdout\",\"stderr\"]}}},"
  "\"required\":[\"n\"]}},"

"{\"name\":\"log_head\",\"description\":\"Emit first N log lines.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"n\":{\"type\":\"integer\"},"
  "\"streams\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"stdout\",\"stderr\"]}}},"
  "\"required\":[\"n\"]}},"

"{\"name\":\"log_range\",\"description\":\"Emit log lines from line a to b (1-based, inclusive).\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"a\":{\"type\":\"integer\"},\"b\":{\"type\":\"integer\"},"
  "\"streams\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"stdout\",\"stderr\"]}}},"
  "\"required\":[\"a\",\"b\"]}},"

"{\"name\":\"log_grep\",\"description\":\"Search log with POSIX extended regex.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"pattern\":{\"type\":\"string\"},"
  "\"ctx\":{\"type\":\"integer\",\"description\":\"context lines around each match\"},"
  "\"streams\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"stdout\",\"stderr\"]}}},"
  "\"required\":[\"pattern\"]}},"

"{\"name\":\"log_where\",\"description\":\"Structural filter over JSON log lines. Each predicate is 'key=value' (equality) or 'key~regex' (POSIX extended regex on the field). All predicates must match.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"predicates\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
  "\"streams\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"stdout\",\"stderr\"]}}},"
  "\"required\":[\"predicates\"]}},"

"{\"name\":\"log_jq\",\"description\":\"Pipe JSON log lines through jq. Non-JSON lines are skipped. Syntax errors or non-zero exit come back as ERR jq: <reason>.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"expr\":{\"type\":\"string\",\"description\":\"jq expression\"},"
  "\"streams\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"stdout\",\"stderr\"]}}},"
  "\"required\":[\"expr\"]}},"

"{\"name\":\"log_clear\",\"description\":\"Clear log buffer(s).\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"streams\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"stdout\",\"stderr\"]}}}}},"

/* control */
"{\"name\":\"action\",\"description\":\"Trigger a named game action. mode: press (default), hold, release.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"name\":{\"type\":\"string\"},"
  "\"mode\":{\"type\":\"string\",\"enum\":[\"press\",\"hold\",\"release\"]}},"
  "\"required\":[\"name\"]}},"

"{\"name\":\"step\",\"description\":\"Advance N frames (game must be paused).\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"n\":{\"type\":\"integer\"}}}},"

"{\"name\":\"pause\",\"description\":\"Pause the game.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"resume\",\"description\":\"Resume the game.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"seed\",\"description\":\"Set the deterministic RNG seed.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"n\":{\"type\":\"integer\"}},\"required\":[\"n\"]}},"

"{\"name\":\"screenshot\",\"description\":\"Capture screen or region. Returns base64 PNG unless 'file' is set.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},"
  "\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"},"
  "\"file\":{\"type\":\"string\",\"description\":\"Save to PATH instead of returning base64\"}}}},"

"{\"name\":\"read_pixel\",\"description\":\"Read RGB at screen coordinate.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"}},"
  "\"required\":[\"x\",\"y\"]}},"

"{\"name\":\"query\",\"description\":\"Query game state: frame, size, fps, or a named var.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"what\":{\"type\":\"string\",\"enum\":[\"frame\",\"size\",\"fps\",\"var\"]},"
  "\"name\":{\"type\":\"string\"}},"
  "\"required\":[\"what\"]}},"

"{\"name\":\"list_actions\",\"description\":\"List registered game actions.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"list_vars\",\"description\":\"List registered state variables.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

/* sessions */
"{\"name\":\"session_info\",\"description\":\"Report current session id/name/attached state.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"session_name\",\"description\":\"Assign a stable name to this session.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}},"

"{\"name\":\"session_nokill\",\"description\":\"Keep the game running after disconnect.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"session_list\",\"description\":\"List all active sessions on the launcher.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"session_kill\",\"description\":\"Force-destroy a named session.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}},"

/* meta */
"{\"name\":\"version\",\"description\":\"Return launcher version.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"ping\",\"description\":\"Health-check the launcher.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"help\",\"description\":\"List launcher commands, or describe one.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"command\":{\"type\":\"string\"}}}},"

/* gdb / crash forensics */
"{\"name\":\"gdb_hint\",\"description\":\"Return PID and a suggested `gdb -p` command to attach to the running game.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"gdb_core_find\",\"description\":\"Locate the most recent core file for the spawned binary.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"gdb_core_list\",\"description\":\"List available cores for the spawned binary (via coredumpctl).\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

"{\"name\":\"gdb_core_summary\",\"description\":\"One-line crash summary (file:line in func: SIGNAME).\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"core\":{\"type\":\"string\",\"description\":\"path to core (defaults to most recent)\"}}}},"

"{\"name\":\"gdb_core_backtrace\",\"description\":\"Full backtrace from a core.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"core\":{\"type\":\"string\"},"
  "\"limit\":{\"type\":\"integer\",\"description\":\"max frames\"}}}},"

"{\"name\":\"gdb_core_frame\",\"description\":\"Info for a specific frame in the backtrace.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"frame\":{\"type\":\"integer\"},"
  "\"core\":{\"type\":\"string\"}},\"required\":[\"frame\"]}},"

"{\"name\":\"gdb_core_locals\",\"description\":\"Local variables at a given frame.\","
 "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
  "\"frame\":{\"type\":\"integer\"},"
  "\"core\":{\"type\":\"string\"}}}}"
"]";
/* clang-format on */

/* ---- Tool call dispatch ---- */

/* Shell-safe-ish: reject whitespace/newline injection into a positional
 * argument. The launcher uses a simple whitespace tokenizer, so arguments
 * must be single tokens. Returns 0 if ok, -1 if invalid. */
static int
check_token(const char *s)
{
	if (!s || !*s) return -1;
	while (*s) {
		if (*s == '\n' || *s == '\r') return -1;
		s++;
	}
	return 0;
}

/* Append a double-quoted, backslash-escaped copy of src to dst (with a
 * preceding space).  The launcher's tokenizer recognises "..." with \ as
 * an in-string escape.  Returns 0 on success, -1 if truncated or if src
 * contains a newline (which would break protocol framing). */
static int
append_quoted(char *dst, size_t cap, const char *src)
{
	size_t len = strlen(dst);
	const char *p;
	if (!src) return -1;
	for (p = src; *p; p++)
		if (*p == '\n' || *p == '\r') return -1;
	if (len + 3 >= cap) return -1;
	dst[len++] = ' ';
	dst[len++] = '"';
	for (p = src; *p; p++) {
		if (*p == '"' || *p == '\\') {
			if (len + 3 >= cap) return -1;
			dst[len++] = '\\';
		} else if (len + 2 >= cap) {
			return -1;
		}
		dst[len++] = *p;
	}
	if (len + 2 >= cap) return -1;
	dst[len++] = '"';
	dst[len] = '\0';
	return 0;
}

static void
handle_tool_call(const char *json, const jsmntok_t *t, int ntok,
                 const char *id_raw, int params_idx)
{
	char name[64] = "";
	int name_idx, args_idx;
	char cmd[8192];

	name_idx = json_obj_find(json, t, ntok, params_idx, "name");
	if (name_idx < 0) {
		result_error(id_raw, -32602, "missing tool name");
		return;
	}
	tok_str(json, &t[name_idx], name, sizeof(name));
	args_idx = json_obj_find(json, t, ntok, params_idx, "arguments");

	/* -------- lifecycle -------- */
	if (strcmp(name, "spawn") == 0) {
		char alias[256] = "";
		get_arg_str(json, t, ntok, args_idx, "alias", alias, sizeof(alias));
		if (!alias[0] || check_token(alias) < 0) {
			result_text(id_raw, "missing or invalid alias", 24, 1);
			return;
		}
		snprintf(cmd, sizeof(cmd), "spawn %s", alias);
		append_str_array(json, t, ntok, args_idx, "args",
		                 cmd, sizeof(cmd));
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "kill") == 0) {
		char sig[32] = "";
		get_arg_str(json, t, ntok, args_idx, "signal", sig, sizeof(sig));
		if (sig[0] && check_token(sig) == 0)
			snprintf(cmd, sizeof(cmd), "kill %s", sig);
		else
			snprintf(cmd, sizeof(cmd), "kill");
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "status") == 0) {
		dispatch_cmd(id_raw, "status", 0, NULL);
		return;
	}
	if (strcmp(name, "env") == 0) {
		char k[128] = "", v[1024] = "";
		get_arg_str(json, t, ntok, args_idx, "key", k, sizeof(k));
		get_arg_str(json, t, ntok, args_idx, "value", v, sizeof(v));
		if (!k[0] || check_token(k) < 0) {
			result_text(id_raw, "missing or invalid key", 22, 1);
			return;
		}
		if (v[0]) {
			if (check_token(v) < 0) {
				result_text(id_raw, "invalid value (no whitespace)", 29, 1);
				return;
			}
			snprintf(cmd, sizeof(cmd), "env %s %s", k, v);
		} else {
			snprintf(cmd, sizeof(cmd), "env %s", k);
		}
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "unsetenv") == 0) {
		char k[128] = "";
		get_arg_str(json, t, ntok, args_idx, "key", k, sizeof(k));
		if (!k[0] || check_token(k) < 0) {
			result_text(id_raw, "missing or invalid key", 22, 1);
			return;
		}
		snprintf(cmd, sizeof(cmd), "unsetenv %s", k);
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}

	/* -------- logs (multi-line) -------- */
	if (strcmp(name, "log_tail") == 0 ||
	    strcmp(name, "log_head") == 0) {
		int n = 10;
		get_arg_int(json, t, ntok, args_idx, "n", &n);
		snprintf(cmd, sizeof(cmd), "%s %d", name, n);
		append_str_array(json, t, ntok, args_idx, "streams",
		                 cmd, sizeof(cmd));
		dispatch_cmd(id_raw, cmd, 1, NULL);
		return;
	}
	if (strcmp(name, "log_range") == 0) {
		int a = 1, b = 1;
		get_arg_int(json, t, ntok, args_idx, "a", &a);
		get_arg_int(json, t, ntok, args_idx, "b", &b);
		snprintf(cmd, sizeof(cmd), "log_range %d %d", a, b);
		append_str_array(json, t, ntok, args_idx, "streams",
		                 cmd, sizeof(cmd));
		dispatch_cmd(id_raw, cmd, 1, NULL);
		return;
	}
	if (strcmp(name, "log_grep") == 0) {
		char pat[1024] = "";
		int ctx = 0;
		get_arg_str(json, t, ntok, args_idx, "pattern", pat, sizeof(pat));
		if (!pat[0] || check_token(pat) < 0) {
			result_text(id_raw, "missing or invalid pattern", 26, 1);
			return;
		}
		if (get_arg_int(json, t, ntok, args_idx, "ctx", &ctx) == 0 &&
		    ctx > 0)
			snprintf(cmd, sizeof(cmd), "log_grep --ctx=%d %s", ctx, pat);
		else
			snprintf(cmd, sizeof(cmd), "log_grep %s", pat);
		append_str_array(json, t, ntok, args_idx, "streams",
		                 cmd, sizeof(cmd));
		dispatch_cmd(id_raw, cmd, 1, NULL);
		return;
	}
	if (strcmp(name, "log_where") == 0) {
		int vi, i, count = 0;
		snprintf(cmd, sizeof(cmd), "log_where");
		vi = args_idx >= 0
		    ? json_obj_find(json, t, ntok, args_idx, "predicates")
		    : -1;
		if (vi < 0 || t[vi].type != JSMN_ARRAY || t[vi].size == 0) {
			result_text(id_raw, "missing or empty predicates", 27, 1);
			return;
		}
		for (i = 0; i < t[vi].size; i++) {
			int ei = vi + 1 + i;
			char pred[512];
			if (ei >= ntok) break;
			tok_str(json, &t[ei], pred, sizeof(pred));
			if (append_quoted(cmd, sizeof(cmd), pred) < 0) {
				result_text(id_raw, "predicate too long or invalid",
				    29, 1);
				return;
			}
			count++;
		}
		if (count == 0) {
			result_text(id_raw, "no valid predicates", 19, 1);
			return;
		}
		append_str_array(json, t, ntok, args_idx, "streams",
		                 cmd, sizeof(cmd));
		dispatch_cmd(id_raw, cmd, 1, NULL);
		return;
	}
	if (strcmp(name, "log_jq") == 0) {
		char expr[2048] = "";
		get_arg_str(json, t, ntok, args_idx, "expr", expr, sizeof(expr));
		if (!expr[0]) {
			result_text(id_raw, "missing expr", 12, 1);
			return;
		}
		snprintf(cmd, sizeof(cmd), "log_jq");
		if (append_quoted(cmd, sizeof(cmd), expr) < 0) {
			result_text(id_raw, "expr too long or invalid", 24, 1);
			return;
		}
		append_str_array(json, t, ntok, args_idx, "streams",
		                 cmd, sizeof(cmd));
		dispatch_cmd(id_raw, cmd, 1, NULL);
		return;
	}
	if (strcmp(name, "log_clear") == 0) {
		snprintf(cmd, sizeof(cmd), "log_clear");
		append_str_array(json, t, ntok, args_idx, "streams",
		                 cmd, sizeof(cmd));
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}

	/* -------- control (proxied to game ctl fd) -------- */
	if (strcmp(name, "action") == 0) {
		char an[64] = "", mode[16] = "";
		get_arg_str(json, t, ntok, args_idx, "name", an, sizeof(an));
		get_arg_str(json, t, ntok, args_idx, "mode", mode, sizeof(mode));
		if (!an[0] || check_token(an) < 0) {
			result_text(id_raw, "missing or invalid action name", 30, 1);
			return;
		}
		if (strcmp(mode, "hold") == 0 || strcmp(mode, "release") == 0)
			snprintf(cmd, sizeof(cmd), "action %s %s", an, mode);
		else
			snprintf(cmd, sizeof(cmd), "action %s", an);
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "step") == 0) {
		int n = 1;
		get_arg_int(json, t, ntok, args_idx, "n", &n);
		if (n < 1) n = 1;
		snprintf(cmd, sizeof(cmd), "step %d", n);
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "pause") == 0) {
		dispatch_cmd(id_raw, "pause", 0, NULL);
		return;
	}
	if (strcmp(name, "resume") == 0) {
		dispatch_cmd(id_raw, "resume", 0, NULL);
		return;
	}
	if (strcmp(name, "seed") == 0) {
		int n = 0;
		get_arg_int(json, t, ntok, args_idx, "n", &n);
		snprintf(cmd, sizeof(cmd), "seed %d", n);
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "screenshot") == 0) {
		int x = -1, y = -1, w = -1, h = -1;
		char file[512] = "";
		int have_rect;
		get_arg_int(json, t, ntok, args_idx, "x", &x);
		get_arg_int(json, t, ntok, args_idx, "y", &y);
		get_arg_int(json, t, ntok, args_idx, "width", &w);
		get_arg_int(json, t, ntok, args_idx, "height", &h);
		get_arg_str(json, t, ntok, args_idx, "file", file, sizeof(file));
		have_rect = (x >= 0 && y >= 0 && w > 0 && h > 0);
		if (file[0]) {
			if (check_token(file) < 0) {
				result_text(id_raw, "invalid file path", 17, 1);
				return;
			}
			if (have_rect)
				snprintf(cmd, sizeof(cmd),
				         "screenshot %d %d %d %d --file=%s",
				         x, y, w, h, file);
			else
				snprintf(cmd, sizeof(cmd),
				         "screenshot --file=%s", file);
			dispatch_cmd(id_raw, cmd, 0, NULL);
		} else {
			if (have_rect)
				snprintf(cmd, sizeof(cmd),
				         "screenshot %d %d %d %d --base64",
				         x, y, w, h);
			else
				snprintf(cmd, sizeof(cmd), "screenshot --base64");
			dispatch_cmd(id_raw, cmd, 0, "base64:");
		}
		return;
	}
	if (strcmp(name, "read_pixel") == 0) {
		int x = 0, y = 0;
		get_arg_int(json, t, ntok, args_idx, "x", &x);
		get_arg_int(json, t, ntok, args_idx, "y", &y);
		snprintf(cmd, sizeof(cmd), "read_pixel %d %d", x, y);
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "query") == 0) {
		char what[16] = "", vname[64] = "";
		get_arg_str(json, t, ntok, args_idx, "what", what, sizeof(what));
		get_arg_str(json, t, ntok, args_idx, "name", vname, sizeof(vname));
		if (!what[0] || check_token(what) < 0) {
			result_text(id_raw, "missing 'what'", 14, 1);
			return;
		}
		if (vname[0] && check_token(vname) == 0)
			snprintf(cmd, sizeof(cmd), "query %s %s", what, vname);
		else
			snprintf(cmd, sizeof(cmd), "query %s", what);
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "list_actions") == 0) {
		dispatch_cmd(id_raw, "list_actions", 0, NULL);
		return;
	}
	if (strcmp(name, "list_vars") == 0) {
		dispatch_cmd(id_raw, "list_vars", 0, NULL);
		return;
	}

	/* -------- sessions -------- */
	if (strcmp(name, "session_info") == 0) {
		dispatch_cmd(id_raw, "session_info", 0, NULL);
		return;
	}
	if (strcmp(name, "session_name") == 0) {
		char nm[128] = "";
		get_arg_str(json, t, ntok, args_idx, "name", nm, sizeof(nm));
		if (!nm[0] || check_token(nm) < 0) {
			result_text(id_raw, "missing or invalid name", 23, 1);
			return;
		}
		snprintf(cmd, sizeof(cmd), "session_name %s", nm);
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "session_nokill") == 0) {
		dispatch_cmd(id_raw, "session_nokill", 0, NULL);
		return;
	}
	if (strcmp(name, "session_list") == 0) {
		dispatch_cmd(id_raw, "session_list", 0, NULL);
		return;
	}
	if (strcmp(name, "session_kill") == 0) {
		char nm[128] = "";
		get_arg_str(json, t, ntok, args_idx, "name", nm, sizeof(nm));
		if (!nm[0] || check_token(nm) < 0) {
			result_text(id_raw, "missing or invalid name", 23, 1);
			return;
		}
		snprintf(cmd, sizeof(cmd), "session_kill %s", nm);
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}

	/* -------- meta -------- */
	if (strcmp(name, "version") == 0) {
		dispatch_cmd(id_raw, "version", 0, NULL);
		return;
	}
	if (strcmp(name, "ping") == 0) {
		dispatch_cmd(id_raw, "ping", 0, NULL);
		return;
	}
	if (strcmp(name, "help") == 0) {
		char c[64] = "";
		get_arg_str(json, t, ntok, args_idx, "command", c, sizeof(c));
		if (c[0] && check_token(c) == 0)
			snprintf(cmd, sizeof(cmd), "help %s", c);
		else
			snprintf(cmd, sizeof(cmd), "help");
		dispatch_cmd(id_raw, cmd, 1, NULL);
		return;
	}

	/* -------- gdb / crash forensics -------- */
	if (strcmp(name, "gdb_hint") == 0) {
		dispatch_cmd(id_raw, "gdb_hint", 0, NULL);
		return;
	}
	if (strcmp(name, "gdb_core_find") == 0) {
		dispatch_cmd(id_raw, "gdb_core_find", 0, NULL);
		return;
	}
	if (strcmp(name, "gdb_core_list") == 0) {
		dispatch_cmd(id_raw, "gdb_core_list", 1, NULL);
		return;
	}
	if (strcmp(name, "gdb_core_summary") == 0) {
		char core[240] = "";
		get_arg_str(json, t, ntok, args_idx, "core", core, sizeof(core));
		if (core[0]) {
			if (check_token(core) < 0) {
				result_text(id_raw, "invalid core path", 17, 1);
				return;
			}
			snprintf(cmd, sizeof(cmd), "gdb_core_summary --core=%s", core);
		} else {
			snprintf(cmd, sizeof(cmd), "gdb_core_summary");
		}
		dispatch_cmd(id_raw, cmd, 0, NULL);
		return;
	}
	if (strcmp(name, "gdb_core_backtrace") == 0) {
		char core[240] = "";
		int limit = 0;
		char extra[256] = "";
		get_arg_str(json, t, ntok, args_idx, "core", core, sizeof(core));
		get_arg_int(json, t, ntok, args_idx, "limit", &limit);
		if (core[0]) {
			if (check_token(core) < 0) {
				result_text(id_raw, "invalid core path", 17, 1);
				return;
			}
			snprintf(extra, sizeof(extra), " --core=%s", core);
		}
		if (limit > 0) {
			size_t n = strlen(extra);
			snprintf(extra + n, sizeof(extra) - n, " --limit=%d", limit);
		}
		snprintf(cmd, sizeof(cmd), "gdb_core_backtrace%s", extra);
		dispatch_cmd(id_raw, cmd, 1, NULL);
		return;
	}
	if (strcmp(name, "gdb_core_frame") == 0) {
		char core[240] = "";
		int frame = 0;
		get_arg_str(json, t, ntok, args_idx, "core", core, sizeof(core));
		get_arg_int(json, t, ntok, args_idx, "frame", &frame);
		if (core[0] && check_token(core) < 0) {
			result_text(id_raw, "invalid core path", 17, 1);
			return;
		}
		if (core[0])
			snprintf(cmd, sizeof(cmd),
			    "gdb_core_frame --frame=%d --core=%s", frame, core);
		else
			snprintf(cmd, sizeof(cmd),
			    "gdb_core_frame --frame=%d", frame);
		dispatch_cmd(id_raw, cmd, 1, NULL);
		return;
	}
	if (strcmp(name, "gdb_core_locals") == 0) {
		char core[240] = "";
		int frame = 0;
		char extra[256] = "";
		get_arg_str(json, t, ntok, args_idx, "core", core, sizeof(core));
		get_arg_int(json, t, ntok, args_idx, "frame", &frame);
		if (core[0] && check_token(core) < 0) {
			result_text(id_raw, "invalid core path", 17, 1);
			return;
		}
		if (frame > 0)
			snprintf(extra, sizeof(extra), " --frame=%d", frame);
		if (core[0]) {
			size_t n = strlen(extra);
			snprintf(extra + n, sizeof(extra) - n, " --core=%s", core);
		}
		snprintf(cmd, sizeof(cmd), "gdb_core_locals%s", extra);
		dispatch_cmd(id_raw, cmd, 1, NULL);
		return;
	}

	/* placeholder: allow parent to suppress unused warning */
	(void)get_arg_bool;

	result_text(id_raw, "unknown tool", 12, 1);
}

/* ---- JSON-RPC message dispatch ---- */

static void
handle_message(const char *json, int len)
{
	jsmn_parser parser;
	int ntok, method_idx, id_idx, params_idx;
	char method[64] = "";
	char id_raw[64] = "null";

	jsmn_init(&parser);
	ntok = jsmn_parse(&parser, json, (size_t)len, tokens, TOK_MAX);
	if (ntok < 1 || tokens[0].type != JSMN_OBJECT) {
		logmsg("invalid JSON-RPC message");
		return;
	}

	method_idx = json_obj_find(json, tokens, ntok, 0, "method");
	if (method_idx < 0)
		return;
	tok_str(json, &tokens[method_idx], method, sizeof(method));

	id_idx = json_obj_find(json, tokens, ntok, 0, "id");
	if (id_idx >= 0) {
		int s = tokens[id_idx].start;
		int e = tokens[id_idx].end;
		int idlen = e - s;
		if (tokens[id_idx].type == JSMN_STRING) {
			snprintf(id_raw, sizeof(id_raw), "\"%.*s\"",
			         idlen, json + s);
		} else {
			if (idlen >= (int)sizeof(id_raw))
				idlen = (int)sizeof(id_raw) - 1;
			memcpy(id_raw, json + s, (size_t)idlen);
			id_raw[idlen] = '\0';
		}
	}

	params_idx = json_obj_find(json, tokens, ntok, 0, "params");

	if (strcmp(method, "initialize") == 0) {
		snprintf(resp_buf, RESP_MAX_SZ,
			"{\"jsonrpc\":\"2.0\",\"id\":%s,"
			"\"result\":{"
			"\"protocolVersion\":\"2025-03-26\","
			"\"capabilities\":{\"tools\":{}},"
			"\"serverInfo\":{\"name\":\"ludica-mcp-bridge\","
			"\"version\":\"1.0.0\"}}}",
			id_raw);
		send_response(resp_buf);
	} else if (strcmp(method, "notifications/initialized") == 0) {
		/* no response */
	} else if (strcmp(method, "tools/list") == 0) {
		snprintf(resp_buf, RESP_MAX_SZ,
			"{\"jsonrpc\":\"2.0\",\"id\":%s,"
			"\"result\":{\"tools\":%s}}",
			id_raw, tools_json);
		send_response(resp_buf);
	} else if (strcmp(method, "tools/call") == 0) {
		if (params_idx < 0)
			result_error(id_raw, -32602, "missing params");
		else
			handle_tool_call(json, tokens, ntok, id_raw, params_idx);
	} else if (strcmp(method, "ping") == 0) {
		snprintf(resp_buf, RESP_MAX_SZ,
			"{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}",
			id_raw);
		send_response(resp_buf);
	} else {
		if (id_idx >= 0)
			result_error(id_raw, -32601, "method not found");
	}
}

int
main(int argc, char **argv)
{
	int i;
	const char *env;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			game_port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-h") == 0 ||
		           strcmp(argv[i], "--help") == 0) {
			fprintf(stderr,
			    "usage: ludica-mcp-bridge [--port N]\n"
			    "env: LUDICA_MCP_PORT (default 4000)\n");
			return 0;
		}
	}
	env = getenv("LUDICA_MCP_PORT");
	if (env && *env)
		game_port = atoi(env);

	resp_buf = malloc(RESP_MAX_SZ);
	tcp_buf = malloc(TCP_BUF_SZ);
	if (!resp_buf || !tcp_buf) {
		fprintf(stderr, "ludica-mcp-bridge: out of memory\n");
		return 1;
	}

	logmsg("started (port=%d)", game_port);

	while (fgets(line_buf, sizeof(line_buf), stdin)) {
		int len = (int)strlen(line_buf);
		while (len > 0 && (line_buf[len - 1] == '\n' ||
		       line_buf[len - 1] == '\r' || line_buf[len - 1] == ' '))
			line_buf[--len] = '\0';
		if (len == 0)
			continue;
		handle_message(line_buf, len);
	}

	tcp_drop();
	logmsg("exiting");
	return 0;
}
