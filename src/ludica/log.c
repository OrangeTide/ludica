#include "ludica.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/* Monotonic milliseconds since first log call. */
static unsigned long long
mono_ms(void)
{
	static int have_base;
	static unsigned long long base_ms;
	unsigned long long now;
#if defined(_WIN32)
	now = (unsigned long long)GetTickCount64();
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	now = (unsigned long long)ts.tv_sec * 1000ull
	    + (unsigned long long)(ts.tv_nsec / 1000000);
#endif
	if (!have_base) { base_ms = now; have_base = 1; }
	return now - base_ms;
}

static const char *
level_name(lud_log_level_t lvl)
{
	switch (lvl) {
	case LUD_LOG_DEBUG: return "debug";
	case LUD_LOG_INFO:  return "info";
	case LUD_LOG_WARN:  return "warn";
	case LUD_LOG_ERROR: return "error";
	}
	return "info";
}

/* Append-with-bounds helper; buf[0..len) holds the current content, cap is
 * the buffer capacity including the terminating NUL.  Returns new len. */
static size_t
sbuf_putc(char *buf, size_t len, size_t cap, char c)
{
	if (len + 1 < cap) buf[len++] = c;
	return len;
}

static size_t
sbuf_puts(char *buf, size_t len, size_t cap, const char *s)
{
	while (*s && len + 1 < cap) buf[len++] = *s++;
	return len;
}

/* Write s as a JSON string fragment (quotes included). */
static size_t
sbuf_json_str(char *buf, size_t len, size_t cap, const char *s)
{
	len = sbuf_putc(buf, len, cap, '"');
	if (!s) s = "";
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		switch (c) {
		case '"':  len = sbuf_puts(buf, len, cap, "\\\""); break;
		case '\\': len = sbuf_puts(buf, len, cap, "\\\\"); break;
		case '\b': len = sbuf_puts(buf, len, cap, "\\b");  break;
		case '\f': len = sbuf_puts(buf, len, cap, "\\f");  break;
		case '\n': len = sbuf_puts(buf, len, cap, "\\n");  break;
		case '\r': len = sbuf_puts(buf, len, cap, "\\r");  break;
		case '\t': len = sbuf_puts(buf, len, cap, "\\t");  break;
		default:
			if (c < 0x20) {
				char esc[8];
				snprintf(esc, sizeof(esc), "\\u%04x", c);
				len = sbuf_puts(buf, len, cap, esc);
			} else {
				len = sbuf_putc(buf, len, cap, (char)c);
			}
		}
	}
	return sbuf_putc(buf, len, cap, '"');
}

static size_t
sbuf_header(char *buf, size_t cap, lud_log_level_t lvl)
{
	int n = snprintf(buf, cap, "{\"t\":%llu,\"lvl\":\"%s\",\"msg\":",
	                 mono_ms(), level_name(lvl));
	if (n < 0) return 0;
	if ((size_t)n >= cap) return cap - 1;
	return (size_t)n;
}

/* Emit one completed JSON log line to stderr. */
static void
emit_line(const char *buf, size_t len)
{
	fwrite(buf, 1, len, stderr);
	fputc('\n', stderr);
	fflush(stderr);
}

void
lud_log(const char *msg, ...)
{
	char buf[1024], fmt[768];
	va_list ap;
	va_start(ap, msg);
	vsnprintf(fmt, sizeof(fmt), msg, ap);
	va_end(ap);

	size_t len = sbuf_header(buf, sizeof(buf), LUD_LOG_INFO);
	len = sbuf_json_str(buf, len, sizeof(buf), fmt);
	len = sbuf_putc(buf, len, sizeof(buf), '}');
	emit_line(buf, len);
}

void
lud_err(const char *msg, ...)
{
	char buf[1024], fmt[768];
	va_list ap;
	va_start(ap, msg);
	vsnprintf(fmt, sizeof(fmt), msg, ap);
	va_end(ap);

	size_t len = sbuf_header(buf, sizeof(buf), LUD_LOG_ERROR);
	len = sbuf_json_str(buf, len, sizeof(buf), fmt);
	len = sbuf_putc(buf, len, sizeof(buf), '}');
	emit_line(buf, len);

#if defined(_WIN32)
	MessageBoxA(NULL, fmt, "Error", MB_OK);
#endif
}

void
lud_logj(lud_log_level_t lvl, const char *msg, ...)
{
	char buf[1024];
	size_t len = sbuf_header(buf, sizeof(buf), lvl);
	len = sbuf_json_str(buf, len, sizeof(buf), msg ? msg : "");

	va_list ap;
	va_start(ap, msg);
	for (;;) {
		const char *key = va_arg(ap, const char *);
		if (!key) break;
		int tag = va_arg(ap, int);
		len = sbuf_putc(buf, len, sizeof(buf), ',');
		len = sbuf_json_str(buf, len, sizeof(buf), key);
		len = sbuf_putc(buf, len, sizeof(buf), ':');
		char num[64];
		switch (tag) {
		case LUD_LOGV_STR: {
			const char *v = va_arg(ap, const char *);
			len = sbuf_json_str(buf, len, sizeof(buf), v ? v : "");
			break;
		}
		case LUD_LOGV_INT: {
			long long v = va_arg(ap, long long);
			snprintf(num, sizeof(num), "%lld", v);
			len = sbuf_puts(buf, len, sizeof(buf), num);
			break;
		}
		case LUD_LOGV_UINT: {
			unsigned long long v = va_arg(ap, unsigned long long);
			snprintf(num, sizeof(num), "%llu", v);
			len = sbuf_puts(buf, len, sizeof(buf), num);
			break;
		}
		case LUD_LOGV_HEX: {
			unsigned long long v = va_arg(ap, unsigned long long);
			snprintf(num, sizeof(num), "\"0x%llx\"", v);
			len = sbuf_puts(buf, len, sizeof(buf), num);
			break;
		}
		case LUD_LOGV_FLT: {
			double v = va_arg(ap, double);
			snprintf(num, sizeof(num), "%.9g", v);
			len = sbuf_puts(buf, len, sizeof(buf), num);
			break;
		}
		case LUD_LOGV_BOOL: {
			int v = va_arg(ap, int);
			len = sbuf_puts(buf, len, sizeof(buf), v ? "true" : "false");
			break;
		}
		default:
			/* Unknown tag: close JSON best-effort and stop. */
			va_end(ap);
			len = sbuf_puts(buf, len, sizeof(buf), "null");
			goto done;
		}
	}
	va_end(ap);
done:
	len = sbuf_putc(buf, len, sizeof(buf), '}');
	emit_line(buf, len);
}
