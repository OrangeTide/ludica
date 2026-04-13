#include "lithos_internal.h"

#if defined(__linux__) || defined(__APPLE__)

#include <time.h>

unsigned long long
lithos__clock_now(void)
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return (unsigned long long)tp.tv_sec * 1000000000ULL + (unsigned long long)tp.tv_nsec;
}

#elif defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

unsigned long long
lithos__clock_now(void)
{
	static LARGE_INTEGER freq;
	LARGE_INTEGER counter;
	if (!freq.QuadPart) {
		QueryPerformanceFrequency(&freq);
	}
	QueryPerformanceCounter(&counter);
	/* convert to nanoseconds */
	return (unsigned long long)(counter.QuadPart * 1000000000ULL / freq.QuadPart);
}

#else
#error "Unsupported platform for timing"
#endif

double
lithos__clock_diff(unsigned long long t1, unsigned long long t0)
{
	return (double)(t1 - t0) / 1e9;
}
