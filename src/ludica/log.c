#include "ludica.h"
#include <stdio.h>
#include <stdarg.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void
lud_log(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void
lud_err(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fputc('\n', stderr);

#if defined(_WIN32)
	{
		char buf[1024];
		va_start(ap, msg);
		vsnprintf(buf, sizeof(buf), msg, ap);
		va_end(ap);
		MessageBoxA(NULL, buf, "Error", MB_OK);
	}
#endif
}
