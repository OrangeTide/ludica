#include <time.h>
struct testtime_info {
	struct timespec ts;
};

void testtime_start(struct testtime_info *ti);
double testtime_end(const struct testtime_info *ti);
