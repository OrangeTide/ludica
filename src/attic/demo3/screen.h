#ifndef SCREEN_H_
#define SCREEN_H_
#include <stdint.h>
void screen_init(int width, int height);
void screen_done(void);
void screen_fill(uint8_t v);
void screen_random(void);
void screen_update(void);
uint8_t *screen_pixels(unsigned *width, unsigned *height, unsigned *rowbytes);
void screen_pattern_herringbone(void);
#endif
