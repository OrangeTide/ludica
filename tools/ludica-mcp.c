/*
 * ludica-mcp.c — MCP (Model Context Protocol) server for ludica
 *
 * Standalone stdio-based MCP server that translates JSON-RPC tool calls
 * into TCP commands against a ludica automation port.
 *
 * Usage: ludica-mcp [--port PORT]
 *
 * Reads newline-delimited JSON-RPC from stdin, writes responses to stdout.
 * Connects to ludica automation server on localhost:PORT (default 4000).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
static void sock_init(void) { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
static void sock_fini(void) { WSACleanup(); }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close close
static void sock_init(void) {}
static void sock_fini(void) {}
#endif

#define JSMN_STATIC
#include "jsmn.h"

/* ---- Limits ---- */

#define LINE_MAX_SZ   (256 * 1024)
#define RESP_MAX_SZ   (256 * 1024)
#define TOK_MAX       512
#define TCP_BUF_SZ    (256 * 1024)

/* ---- Static state ---- */

static sock_t game_fd = SOCK_INVALID;
static int game_port = 4000;
static char line_buf[LINE_MAX_SZ];
static char resp_buf[RESP_MAX_SZ];
static char tcp_buf[TCP_BUF_SZ];
static jsmntok_t tokens[TOK_MAX];

/* ---- Logging (stderr only) ---- */

static void
logmsg(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "ludica-mcp: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

/* ---- JSON helpers ---- */

static int
tok_eq(const char *json, const jsmntok_t *t, const char *s)
{
	int len = t->end - t->start;
	return (t->type == JSMN_STRING &&
	        (int)strlen(s) == len &&
	        strncmp(json + t->start, s, (size_t)len) == 0);
}

/* Copy token value into a buffer, NUL-terminated */
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

/* Count the total number of tokens in the subtree rooted at idx */
static int
tok_skip(const jsmntok_t *t, int ntok, int idx)
{
	int i, j;

	if (idx >= ntok)
		return idx;

	if (t[idx].type == JSMN_OBJECT) {
		j = idx + 1;
		for (i = 0; i < t[idx].size; i++) {
			j = tok_skip(t, ntok, j);  /* key */
			j = tok_skip(t, ntok, j);  /* value */
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

/* Find a key in a JSON object (token at obj_idx).
 * Returns token index of the value, or -1 if not found. */
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
		j = tok_skip(t, ntok, j);     /* skip key */
		j = tok_skip(t, ntok, j);     /* skip value */
	}
	return -1;
}

/* ---- JSON output helpers ---- */

/* Escape a string for JSON output. Returns number of bytes written. */
static int
json_escape(char *out, size_t outsz, const char *s)
{
	size_t i = 0;
	while (*s && i + 6 < outsz) {
		switch (*s) {
		case '"':  out[i++] = '\\'; out[i++] = '"'; break;
		case '\\': out[i++] = '\\'; out[i++] = '\\'; break;
		case '\n': out[i++] = '\\'; out[i++] = 'n'; break;
		case '\r': out[i++] = '\\'; out[i++] = 'r'; break;
		case '\t': out[i++] = '\\'; out[i++] = 't'; break;
		default:
			if ((unsigned char)*s < 0x20) {
				i += (size_t)snprintf(out + i, outsz - i,
				                      "\\u%04x", (unsigned char)*s);
			} else {
				out[i++] = *s;
			}
			break;
		}
		s++;
	}
	out[i] = '\0';
	return (int)i;
}

/* Write a JSON-RPC response to stdout */
static void
send_response(const char *json)
{
	fputs(json, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

/* ---- TCP connection to ludica ---- */

static int
tcp_connect(void)
{
	struct sockaddr_in addr;

	if (game_fd != SOCK_INVALID)
		return 0;

	game_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (game_fd == SOCK_INVALID) {
		logmsg("socket() failed");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((unsigned short)game_port);

	if (connect(game_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		logmsg("connect(127.0.0.1:%d) failed", game_port);
		sock_close(game_fd);
		game_fd = SOCK_INVALID;
		return -1;
	}

	return 0;
}

/* Send a command to ludica and read the one-line response.
 * Returns response string (static buffer) or NULL on error. */
static char *
tcp_command(const char *cmd)
{
	int len, n, total;
	char *nl;

	if (tcp_connect() < 0)
		return NULL;

	len = (int)strlen(cmd);

	/* Send command + newline */
	if (send(game_fd, cmd, len, 0) != len)
		goto fail;
	if (cmd[len - 1] != '\n') {
		if (send(game_fd, "\n", 1, 0) != 1)
			goto fail;
	}

	/* Read response (one line) */
	total = 0;
	for (;;) {
		n = recv(game_fd, tcp_buf + total, TCP_BUF_SZ - 1 - total, 0);
		if (n <= 0)
			goto fail;
		total += n;
		tcp_buf[total] = '\0';
		nl = strchr(tcp_buf, '\n');
		if (nl) {
			*nl = '\0';
			return tcp_buf;
		}
		if (total >= TCP_BUF_SZ - 1)
			goto fail;
	}

fail:
	sock_close(game_fd);
	game_fd = SOCK_INVALID;
	return NULL;
}

/* ---- Tool definitions ---- */

/* clang-format off */
static const char *tools_json =
	"["
	"{\"name\":\"list_actions\","
	 "\"description\":\"List available game actions and their key bindings\","
	 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

	"{\"name\":\"do_action\","
	 "\"description\":\"Trigger a named game action\","
	 "\"inputSchema\":{\"type\":\"object\","
	  "\"properties\":{"
	   "\"name\":{\"type\":\"string\",\"description\":\"Action name\"},"
	   "\"mode\":{\"type\":\"string\",\"enum\":[\"press\",\"hold\",\"release\"],\"description\":\"press (default), hold, or release\"}"
	  "},\"required\":[\"name\"]}},"

	"{\"name\":\"step\","
	 "\"description\":\"Advance N frames (game must be paused). Returns frame number.\","
	 "\"inputSchema\":{\"type\":\"object\","
	  "\"properties\":{"
	   "\"count\":{\"type\":\"integer\",\"description\":\"Number of frames to advance (default 1)\"}"
	  "}}},"

	"{\"name\":\"screenshot\","
	 "\"description\":\"Capture the screen or a region as a base64 PNG\","
	 "\"inputSchema\":{\"type\":\"object\","
	  "\"properties\":{"
	   "\"x\":{\"type\":\"integer\",\"description\":\"Left edge (for region capture)\"},"
	   "\"y\":{\"type\":\"integer\",\"description\":\"Top edge (for region capture)\"},"
	   "\"width\":{\"type\":\"integer\",\"description\":\"Width (for region capture)\"},"
	   "\"height\":{\"type\":\"integer\",\"description\":\"Height (for region capture)\"},"
	   "\"file\":{\"type\":\"string\",\"description\":\"Save to file instead of returning base64\"}"
	  "}}},"

	"{\"name\":\"read_pixel\","
	 "\"description\":\"Read the RGB color at a screen coordinate\","
	 "\"inputSchema\":{\"type\":\"object\","
	  "\"properties\":{"
	   "\"x\":{\"type\":\"integer\",\"description\":\"X coordinate\"},"
	   "\"y\":{\"type\":\"integer\",\"description\":\"Y coordinate\"}"
	  "},\"required\":[\"x\",\"y\"]}},"

	"{\"name\":\"query\","
	 "\"description\":\"Query game state: frame, size, fps, or a named variable\","
	 "\"inputSchema\":{\"type\":\"object\","
	  "\"properties\":{"
	   "\"what\":{\"type\":\"string\",\"enum\":[\"frame\",\"size\",\"fps\",\"var\"],\"description\":\"What to query\"},"
	   "\"name\":{\"type\":\"string\",\"description\":\"Variable name (when what=var)\"}"
	  "},\"required\":[\"what\"]}},"

	"{\"name\":\"list_vars\","
	 "\"description\":\"List registered game state variables\","
	 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

	"{\"name\":\"send_keys\","
	 "\"description\":\"Send key down/up events\","
	 "\"inputSchema\":{\"type\":\"object\","
	  "\"properties\":{"
	   "\"key\":{\"type\":\"string\",\"description\":\"Key name (e.g. A, ESCAPE, SPACE)\"},"
	   "\"action\":{\"type\":\"string\",\"enum\":[\"press\",\"down\",\"up\"],\"description\":\"press (down+up, default), down, or up\"}"
	  "},\"required\":[\"key\"]}},"

	"{\"name\":\"mouse_click\","
	 "\"description\":\"Click the mouse at a position\","
	 "\"inputSchema\":{\"type\":\"object\","
	  "\"properties\":{"
	   "\"x\":{\"type\":\"integer\",\"description\":\"X coordinate\"},"
	   "\"y\":{\"type\":\"integer\",\"description\":\"Y coordinate\"},"
	   "\"button\":{\"type\":\"integer\",\"description\":\"Mouse button (default 1)\"}"
	  "},\"required\":[\"x\",\"y\"]}},"

	"{\"name\":\"audio_capture\","
	 "\"description\":\"Start or stop audio capture. Stop writes a WAV file.\","
	 "\"inputSchema\":{\"type\":\"object\","
	  "\"properties\":{"
	   "\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"stop\"],\"description\":\"start or stop\"},"
	   "\"file\":{\"type\":\"string\",\"description\":\"Output filename (for stop)\"}"
	  "},\"required\":[\"action\"]}},"

	"{\"name\":\"quit\","
	 "\"description\":\"Request the game to exit\","
	 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

	"{\"name\":\"help\","
	 "\"description\":\"List all automation commands or get detailed help for a specific command\","
	 "\"inputSchema\":{\"type\":\"object\","
	  "\"properties\":{"
	   "\"command\":{\"type\":\"string\",\"description\":\"Command name for detailed help (omit to list all)\"}"
	  "}}},"

	"{\"name\":\"restart\","
	 "\"description\":\"Re-exec the game process with a freshly built binary. Preserves the listen port.\","
	 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

	"{\"name\":\"nokill\","
	 "\"description\":\"Disable auto-terminate so the game stays running after client disconnects\","
	 "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
	"]";
/* clang-format on */

/* ---- Tool call handlers ---- */

/* Build a JSON-RPC result with text content */
static void
result_text(char *out, size_t outsz, const char *id_raw, const char *text,
            int is_error)
{
	char escaped[TCP_BUF_SZ * 2];
	json_escape(escaped, sizeof(escaped), text);
	snprintf(out, outsz,
		"{\"jsonrpc\":\"2.0\",\"id\":%s,"
		"\"result\":{\"content\":[{\"type\":\"text\","
		"\"text\":\"%s\"}],\"isError\":%s}}",
		id_raw, escaped, is_error ? "true" : "false");
}

/* Build a JSON-RPC result with base64 image content */
static void
result_image(char *out, size_t outsz, const char *id_raw,
             const char *b64_data)
{
	snprintf(out, outsz,
		"{\"jsonrpc\":\"2.0\",\"id\":%s,"
		"\"result\":{\"content\":[{\"type\":\"image\","
		"\"data\":\"%s\",\"mimeType\":\"image/png\"}],"
		"\"isError\":false}}",
		id_raw, b64_data);
}

static void
result_error(char *out, size_t outsz, const char *id_raw, int code,
             const char *msg)
{
	char escaped[256];
	json_escape(escaped, sizeof(escaped), msg);
	snprintf(out, outsz,
		"{\"jsonrpc\":\"2.0\",\"id\":%s,"
		"\"error\":{\"code\":%d,\"message\":\"%s\"}}",
		id_raw, code, escaped);
}

/* Get a string argument from the tool call arguments object */
static int
get_arg_str(const char *json, const jsmntok_t *t, int ntok,
            int args_idx, const char *key, char *out, size_t outsz)
{
	int vi;
	if (args_idx < 0)
		return -1;
	vi = json_obj_find(json, t, ntok, args_idx, key);
	if (vi < 0)
		return -1;
	tok_str(json, &t[vi], out, outsz);
	return 0;
}

static int
get_arg_int(const char *json, const jsmntok_t *t, int ntok,
            int args_idx, const char *key, int *out)
{
	int vi;
	if (args_idx < 0)
		return -1;
	vi = json_obj_find(json, t, ntok, args_idx, key);
	if (vi < 0)
		return -1;
	*out = tok_int(json, &t[vi]);
	return 0;
}

static void
handle_tool_call(const char *json, const jsmntok_t *t, int ntok,
                 const char *id_raw, int params_idx)
{
	char name[64] = "";
	int name_idx, args_idx;
	char *reply;

	name_idx = json_obj_find(json, t, ntok, params_idx, "name");
	if (name_idx < 0) {
		result_error(resp_buf, sizeof(resp_buf), id_raw, -32602,
		             "missing tool name");
		send_response(resp_buf);
		return;
	}
	tok_str(json, &t[name_idx], name, sizeof(name));
	args_idx = json_obj_find(json, t, ntok, params_idx, "arguments");

	/* ---- list_actions ---- */
	if (strcmp(name, "list_actions") == 0) {
		reply = tcp_command("LISTACTIONS");
		if (!reply) {
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		} else if (strncmp(reply, "OK ", 3) == 0) {
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            reply + 3, 0);
		} else {
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);
		}

	/* ---- do_action ---- */
	} else if (strcmp(name, "do_action") == 0) {
		char aname[64] = "", mode[16] = "";
		char cmd[128];
		get_arg_str(json, t, ntok, args_idx, "name", aname, sizeof(aname));
		get_arg_str(json, t, ntok, args_idx, "mode", mode, sizeof(mode));
		if (!aname[0]) {
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "missing action name", 1);
			send_response(resp_buf);
			return;
		}
		if (mode[0])
			snprintf(cmd, sizeof(cmd), "ACTION %s %s", aname, mode);
		else
			snprintf(cmd, sizeof(cmd), "ACTION %s", aname);
		reply = tcp_command(cmd);
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK", 2) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw, "OK", 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- step ---- */
	} else if (strcmp(name, "step") == 0) {
		int count = 1;
		char cmd[64];
		get_arg_int(json, t, ntok, args_idx, "count", &count);
		if (count < 1) count = 1;
		snprintf(cmd, sizeof(cmd), "STEP %d", count);
		reply = tcp_command(cmd);
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK", 2) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            reply + 3, 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- screenshot ---- */
	} else if (strcmp(name, "screenshot") == 0) {
		int x = -1, y = -1, w = -1, h = -1;
		char file[256] = "";
		char cmd[512];

		get_arg_int(json, t, ntok, args_idx, "x", &x);
		get_arg_int(json, t, ntok, args_idx, "y", &y);
		get_arg_int(json, t, ntok, args_idx, "width", &w);
		get_arg_int(json, t, ntok, args_idx, "height", &h);
		get_arg_str(json, t, ntok, args_idx, "file", file, sizeof(file));

		if (file[0]) {
			/* Save to file */
			if (x >= 0 && y >= 0 && w > 0 && h > 0)
				snprintf(cmd, sizeof(cmd),
				         "CAPRECT %d %d %d %d %s", x, y, w, h, file);
			else
				snprintf(cmd, sizeof(cmd), "CAPSCREEN %s", file);
		} else {
			/* Return as base64 */
			if (x >= 0 && y >= 0 && w > 0 && h > 0)
				snprintf(cmd, sizeof(cmd),
				         "CAPRECT %d %d %d %d --base64",
				         x, y, w, h);
			else
				snprintf(cmd, sizeof(cmd), "CAPSCREEN --base64");
		}

		reply = tcp_command(cmd);
		if (!reply) {
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		} else if (strncmp(reply, "OK base64:", 10) == 0) {
			result_image(resp_buf, sizeof(resp_buf), id_raw,
			             reply + 10);
		} else if (strncmp(reply, "OK ", 3) == 0) {
			/* file save — pass through the full message (e.g. "saved 800x600 PNG to /path") */
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            reply + 3, 0);
		} else {
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);
		}

	/* ---- read_pixel ---- */
	} else if (strcmp(name, "read_pixel") == 0) {
		int x = 0, y = 0;
		char cmd[64];
		get_arg_int(json, t, ntok, args_idx, "x", &x);
		get_arg_int(json, t, ntok, args_idx, "y", &y);
		snprintf(cmd, sizeof(cmd), "READPIXEL %d %d", x, y);
		reply = tcp_command(cmd);
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK ", 3) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            reply + 3, 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- query ---- */
	} else if (strcmp(name, "query") == 0) {
		char what[16] = "", vname[64] = "";
		char cmd[128];
		get_arg_str(json, t, ntok, args_idx, "what", what, sizeof(what));
		get_arg_str(json, t, ntok, args_idx, "name", vname, sizeof(vname));
		if (!what[0]) {
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "missing 'what' argument", 1);
			send_response(resp_buf);
			return;
		}
		if (vname[0])
			snprintf(cmd, sizeof(cmd), "QUERY %s %s", what, vname);
		else
			snprintf(cmd, sizeof(cmd), "QUERY %s", what);
		reply = tcp_command(cmd);
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK ", 3) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            reply + 3, 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- list_vars ---- */
	} else if (strcmp(name, "list_vars") == 0) {
		reply = tcp_command("LISTVAR");
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK", 2) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            reply + 3, 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- send_keys ---- */
	} else if (strcmp(name, "send_keys") == 0) {
		char key[32] = "", action[16] = "press";
		char cmd[64];
		get_arg_str(json, t, ntok, args_idx, "key", key, sizeof(key));
		get_arg_str(json, t, ntok, args_idx, "action", action,
		            sizeof(action));
		if (!key[0]) {
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "missing key", 1);
			send_response(resp_buf);
			return;
		}
		if (strcmp(action, "down") == 0) {
			snprintf(cmd, sizeof(cmd), "KEYDOWN %s", key);
			reply = tcp_command(cmd);
		} else if (strcmp(action, "up") == 0) {
			snprintf(cmd, sizeof(cmd), "KEYUP %s", key);
			reply = tcp_command(cmd);
		} else {
			/* press = down then up */
			snprintf(cmd, sizeof(cmd), "KEYDOWN %s", key);
			reply = tcp_command(cmd);
			if (reply && strncmp(reply, "OK", 2) == 0) {
				snprintf(cmd, sizeof(cmd), "KEYUP %s", key);
				reply = tcp_command(cmd);
			}
		}
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK", 2) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw, "OK", 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- mouse_click ---- */
	} else if (strcmp(name, "mouse_click") == 0) {
		int x = 0, y = 0, btn = 1;
		char cmd[64];
		get_arg_int(json, t, ntok, args_idx, "x", &x);
		get_arg_int(json, t, ntok, args_idx, "y", &y);
		get_arg_int(json, t, ntok, args_idx, "button", &btn);
		snprintf(cmd, sizeof(cmd), "MOUSEDOWN %d %d %d", btn, x, y);
		reply = tcp_command(cmd);
		if (reply && strncmp(reply, "OK", 2) == 0) {
			snprintf(cmd, sizeof(cmd), "MOUSEUP %d %d %d", btn, x, y);
			reply = tcp_command(cmd);
		}
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK", 2) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw, "OK", 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- audio_capture ---- */
	} else if (strcmp(name, "audio_capture") == 0) {
		char action[16] = "", file[256] = "";
		char cmd[512];
		get_arg_str(json, t, ntok, args_idx, "action", action,
		            sizeof(action));
		get_arg_str(json, t, ntok, args_idx, "file", file, sizeof(file));
		if (strcmp(action, "start") == 0) {
			reply = tcp_command("CAPAUDIO START");
		} else if (strcmp(action, "stop") == 0) {
			if (file[0])
				snprintf(cmd, sizeof(cmd), "CAPAUDIO STOP %s", file);
			else
				snprintf(cmd, sizeof(cmd), "CAPAUDIO STOP");
			reply = tcp_command(cmd);
		} else {
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "action must be 'start' or 'stop'", 1);
			send_response(resp_buf);
			return;
		}
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK", 2) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            reply + (reply[2] == ' ' ? 3 : 2), 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- quit ---- */
	} else if (strcmp(name, "quit") == 0) {
		reply = tcp_command("QUIT");
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, "OK", 0);

	/* ---- help ---- */
	} else if (strcmp(name, "help") == 0) {
		char command[64] = "";
		char cmd[128];
		get_arg_str(json, t, ntok, args_idx, "command", command,
		            sizeof(command));
		if (command[0])
			snprintf(cmd, sizeof(cmd), "HELP %s", command);
		else
			snprintf(cmd, sizeof(cmd), "HELP");
		reply = tcp_command(cmd);
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK ", 3) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            reply + 3, 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- restart ---- */
	} else if (strcmp(name, "restart") == 0) {
		reply = tcp_command("RESTART");
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK", 2) == 0) {
			/* close our connection — the game is re-execing */
			sock_close(game_fd);
			game_fd = SOCK_INVALID;
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "game is restarting", 0);
		} else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	/* ---- nokill ---- */
	} else if (strcmp(name, "nokill") == 0) {
		reply = tcp_command("NOKILL");
		if (!reply)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            "not connected to game", 1);
		else if (strncmp(reply, "OK", 2) == 0)
			result_text(resp_buf, sizeof(resp_buf), id_raw,
			            reply + (reply[2] == ' ' ? 3 : 2), 0);
		else
			result_text(resp_buf, sizeof(resp_buf), id_raw, reply, 1);

	} else {
		result_text(resp_buf, sizeof(resp_buf), id_raw,
		            "unknown tool", 1);
	}

	send_response(resp_buf);
}

/* ---- Message dispatch ---- */

static void
handle_message(const char *json, int len)
{
	jsmn_parser parser;
	int ntok, method_idx, id_idx, params_idx;
	char method[64] = "";
	char id_raw[64] = "";

	jsmn_init(&parser);
	ntok = jsmn_parse(&parser, json, (size_t)len, tokens, TOK_MAX);
	if (ntok < 1 || tokens[0].type != JSMN_OBJECT) {
		logmsg("invalid JSON-RPC message");
		return;
	}

	/* Extract method */
	method_idx = json_obj_find(json, tokens, ntok, 0, "method");
	if (method_idx < 0)
		return;
	tok_str(json, &tokens[method_idx], method, sizeof(method));

	/* Extract id (raw JSON value — could be number or string) */
	id_idx = json_obj_find(json, tokens, ntok, 0, "id");
	if (id_idx >= 0) {
		int s = tokens[id_idx].start;
		int e = tokens[id_idx].end;
		int idlen = e - s;
		if (tokens[id_idx].type == JSMN_STRING) {
			/* Wrap in quotes for JSON output */
			snprintf(id_raw, sizeof(id_raw), "\"%.*s\"", idlen,
			         json + s);
		} else {
			if (idlen >= (int)sizeof(id_raw))
				idlen = (int)sizeof(id_raw) - 1;
			memcpy(id_raw, json + s, (size_t)idlen);
			id_raw[idlen] = '\0';
		}
	}

	/* Extract params */
	params_idx = json_obj_find(json, tokens, ntok, 0, "params");

	/* ---- initialize ---- */
	if (strcmp(method, "initialize") == 0) {
		snprintf(resp_buf, sizeof(resp_buf),
			"{\"jsonrpc\":\"2.0\",\"id\":%s,"
			"\"result\":{"
			"\"protocolVersion\":\"2025-03-26\","
			"\"capabilities\":{\"tools\":{}},"
			"\"serverInfo\":{\"name\":\"ludica-mcp\","
			"\"version\":\"1.0.0\"}}}",
			id_raw);
		send_response(resp_buf);

	/* ---- notifications/initialized ---- */
	} else if (strcmp(method, "notifications/initialized") == 0) {
		/* no response needed */

	/* ---- tools/list ---- */
	} else if (strcmp(method, "tools/list") == 0) {
		snprintf(resp_buf, sizeof(resp_buf),
			"{\"jsonrpc\":\"2.0\",\"id\":%s,"
			"\"result\":{\"tools\":%s}}",
			id_raw, tools_json);
		send_response(resp_buf);

	/* ---- tools/call ---- */
	} else if (strcmp(method, "tools/call") == 0) {
		if (params_idx < 0) {
			result_error(resp_buf, sizeof(resp_buf), id_raw,
			             -32602, "missing params");
			send_response(resp_buf);
		} else {
			handle_tool_call(json, tokens, ntok, id_raw, params_idx);
		}

	/* ---- ping ---- */
	} else if (strcmp(method, "ping") == 0) {
		snprintf(resp_buf, sizeof(resp_buf),
			"{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}",
			id_raw);
		send_response(resp_buf);

	/* ---- unknown method ---- */
	} else {
		if (id_raw[0]) {
			result_error(resp_buf, sizeof(resp_buf), id_raw,
			             -32601, "method not found");
			send_response(resp_buf);
		}
		/* notifications without id are silently ignored */
	}
}

/* ---- Main ---- */

int
main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			game_port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-h") == 0 ||
		           strcmp(argv[i], "--help") == 0) {
			fprintf(stderr, "usage: ludica-mcp [--port PORT]\n");
			return 0;
		}
	}

	sock_init();
	logmsg("started (port=%d)", game_port);

	/* Read newline-delimited JSON-RPC messages from stdin */
	while (fgets(line_buf, sizeof(line_buf), stdin)) {
		int len = (int)strlen(line_buf);

		/* Strip trailing whitespace */
		while (len > 0 && (line_buf[len-1] == '\n' ||
		       line_buf[len-1] == '\r' || line_buf[len-1] == ' '))
			line_buf[--len] = '\0';

		if (len == 0)
			continue;

		handle_message(line_buf, len);
	}

	if (game_fd != SOCK_INVALID)
		sock_close(game_fd);
	sock_fini();
	logmsg("exiting");
	return 0;
}
