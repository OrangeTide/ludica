/* speaker.c - PC speaker emulation for lilpc */
#include "speaker.h"
#include <string.h>

/*
 * The PC speaker is controlled by two signals:
 *   - Port 61h bit 0: PIT channel 2 gate (enables counting)
 *   - Port 61h bit 1: speaker enable (connects PIT output to speaker)
 *
 * When both are set, the PIT channel 2 output drives the speaker.
 * The PIT in mode 3 (square wave) produces a tone at:
 *   frequency = 1193182 / reload_value
 *
 * Some programs bypass the PIT and toggle port 61h bit 1 directly
 * for sample playback (PWM/PCM tricks).
 */

void speaker_init(speaker_t *spkr)
{
	memset(spkr, 0, sizeof(*spkr));
}

void speaker_update(speaker_t *spkr, bool pit_out)
{
	spkr->pit_out = pit_out;

	if (spkr->enabled && spkr->pit_gate) {
		spkr->level = pit_out ? 1.0f : -1.0f;
	} else if (spkr->enabled) {
		/* direct toggle mode (no PIT) */
		spkr->level = 1.0f;
	} else {
		spkr->level = 0.0f;
	}
}

float speaker_sample(speaker_t *spkr)
{
	if (!spkr->enabled)
		return 0.0f;

	return spkr->level * 0.3f; /* attenuate to reasonable volume */
}
