#include "report.h"
#include <stdio.h>
#include <stdarg.h>
void
report_error(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fputc('\n', stderr);
}
