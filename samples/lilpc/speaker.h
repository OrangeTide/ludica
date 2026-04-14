/* speaker.h - PC speaker emulation for lilpc */
#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>
#include <stdbool.h>

struct lilpc;

typedef struct speaker {
	bool enabled;		/* speaker gate (port 61h bit 1) */
	bool pit_gate;		/* PIT channel 2 gate (port 61h bit 0) */
	bool pit_out;		/* PIT channel 2 output */
	float level;		/* current output level (-1.0 to 1.0) */
} speaker_t;

void speaker_init(speaker_t *spkr);
void speaker_update(speaker_t *spkr, bool pit_out);

/* get current sample for audio mixing */
float speaker_sample(speaker_t *spkr);

#endif
