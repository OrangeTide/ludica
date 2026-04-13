#ifndef LUDICA_AUDIO_H_
#define LUDICA_AUDIO_H_

#include <stdint.h>

#define LUD_AUDIO_CHANNELS    16
#define LUD_AUDIO_SAMPLE_RATE 44100

/* Sample formats */
enum lud_audio_fmt {
	LUD_AUDIO_PCM16  = 0,   /* signed 16-bit, native endian */
	LUD_AUDIO_PCM8   = 1,   /* signed 8-bit */
	LUD_AUDIO_ADPCM  = 2,   /* IMA ADPCM (4-bit, high nibble first) */
};

/* Play descriptor — fill and pass to lud_audio_play() */
typedef struct lud_audio_desc {
	const void *data;          /* pointer to sample data */
	unsigned int length;       /* total length in sample frames */
	unsigned int loop_start;   /* loop start in sample frames */
	unsigned int loop_length;  /* loop length in frames (0 = no loop) */
	int volume_l;              /* left volume 0-255 */
	int volume_r;              /* right volume 0-255 */
	int pitch;                 /* 8.8 fixed-point rate (256 = normal speed) */
	enum lud_audio_fmt format;
} lud_audio_desc_t;

/* Initialize audio system (starts playback device). */
void lud_audio_init(void);

/* Shut down audio system. */
void lud_audio_shutdown(void);

/* Play a sample on channel ch (0-15). Restarts if already playing. */
void lud_audio_play(int ch, const lud_audio_desc_t *desc);

/* Stop a channel. */
void lud_audio_stop(int ch);

/* Set master volume (0-255 per side). Default is 255. */
void lud_audio_set_master(int left, int right);

/* Mix nframes of stereo S16 audio into out[].
 * Called automatically from the device callback.
 * May also be called directly for custom backends. */
void lud_audio_mix(int16_t *out, int nframes);

#endif /* LUDICA_AUDIO_H_ */
