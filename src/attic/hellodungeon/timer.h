#ifndef DEMO3_TIMER_H_
#define DEMO3_TIMER_H_
#include <stdbool.h>
void timer_stop(unsigned *t);
void timer_start(unsigned *t);
bool timer_now(unsigned t);
#endif
