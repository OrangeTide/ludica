// TODO: rewrite this to be Windows-friendly
#include "log.h"
#include <stdio.h>
#include <stdarg.h>

void
log_debug(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	fputc('\n', stderr);
	va_end(ap);
}

void
log_error(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fputc('\n', stderr);
}
