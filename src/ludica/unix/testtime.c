#include <stdio.h>
#include "testtime.h"

void
testtime_start(struct testtime_info *ti)
{
	if (clock_gettime(CLOCK_MONOTONIC, &ti->ts))
		perror(__func__);
}

double
testtime_end(const struct testtime_info *ti)
{
	struct timespec now, res;

	if (clock_gettime(CLOCK_MONOTONIC, &now)) {
		perror(__func__);
		return -1;
	}
	if (clock_getres(CLOCK_MONOTONIC, &res)) {
		perror(__func__);
		return -1;
	}
	return (now.tv_sec - ti->ts.tv_sec) +
		(now.tv_nsec - ti->ts.tv_nsec) / 1e9;
}

