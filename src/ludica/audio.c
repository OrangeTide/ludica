/*
 * audio.c — 16-channel PCM audio mixer for ludica
 *
 * Adapted from triton_audio (Jon Mayo, MIT-0 / Public Domain).
 * Supports PCM16, PCM8, and IMA ADPCM sample formats.
 * Uses miniaudio for the platform audio device.
 */

#include "ludica_internal.h"
#include "ludica_audio.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <string.h>

/* ---- IMA ADPCM tables ---- */

static const int16_t ima_step_table[89] = {
	    7,     8,     9,    10,    11,    12,    13,    14,
	   16,    17,    19,    21,    23,    25,    28,    31,
	   34,    37,    41,    45,    50,    55,    60,    66,
	   73,    80,    88,    97,   107,   118,   130,   143,
	  157,   173,   190,   209,   230,   253,   279,   307,
	  337,   371,   408,   449,   494,   544,   598,   658,
	  724,   796,   876,   963,  1060,  1166,  1282,  1411,
	 1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
	 3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
	 7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};

static const int ima_index_table[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};

/* ---- Channel state ---- */

typedef struct {
	/* Descriptor (set by lud_audio_play) */
	const void *data;
	unsigned int length;
	unsigned int loop_start;
	unsigned int loop_length;
	int volume_l, volume_r;
	unsigned int pitch;       /* 8.8 fixed-point */
	int format;
	int loop_en;
	int playing;

	/* Runtime */
	unsigned int position;
	unsigned int frac;
	int16_t adpcm_pred;
	int adpcm_index;
} channel_t;

/* ---- Static state ---- */

static channel_t channels[LUD_AUDIO_CHANNELS];
static int master_vol_l = 255;
static int master_vol_r = 255;
static ma_device device;
static int device_inited;
static ma_spinlock mix_lock;

/* ---- Capture state ---- */

static int16_t *cap_buf;
static size_t cap_frames;
static size_t cap_cap;
static int cap_active;

/* ---- ADPCM decoder ---- */

static int16_t
adpcm_decode_nibble(channel_t *ch, unsigned char nibble)
{
	int step = ima_step_table[ch->adpcm_index];
	int diff = step >> 3;
	int pred;

	if (nibble & 1) diff += step >> 2;
	if (nibble & 2) diff += step >> 1;
	if (nibble & 4) diff += step;
	if (nibble & 8) diff = -diff;

	pred = ch->adpcm_pred + diff;
	if (pred > 32767)  pred = 32767;
	if (pred < -32768) pred = -32768;
	ch->adpcm_pred = (int16_t)pred;

	ch->adpcm_index += ima_index_table[nibble & 0xF];
	if (ch->adpcm_index < 0)  ch->adpcm_index = 0;
	if (ch->adpcm_index > 88) ch->adpcm_index = 88;

	return ch->adpcm_pred;
}

static unsigned char
adpcm_fetch_nibble(const void *data, unsigned int pos)
{
	const unsigned char *p = (const unsigned char *)data;
	unsigned char byte = p[pos / 2];
	return (pos & 1) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
}

/* ---- Sample fetch ---- */

static int16_t
fetch_sample(channel_t *ch)
{
	switch (ch->format) {
	case LUD_AUDIO_PCM16: {
		const int16_t *p = (const int16_t *)ch->data;
		if (ch->position >= ch->length)
			return 0;
		return p[ch->position];
	}
	case LUD_AUDIO_PCM8: {
		const signed char *p = (const signed char *)ch->data;
		if (ch->position >= ch->length)
			return 0;
		return (int16_t)p[ch->position] << 8;
	}
	case LUD_AUDIO_ADPCM:
		return ch->adpcm_pred;
	default:
		return 0;
	}
}

/* ---- Channel advance ---- */

static void
advance_channel(channel_t *ch, unsigned int steps)
{
	unsigned int s;

	for (s = 0; s < steps; s++) {
		if (ch->format == LUD_AUDIO_ADPCM) {
			unsigned char nib = adpcm_fetch_nibble(ch->data,
			                                       ch->position);
			adpcm_decode_nibble(ch, nib);
		}

		ch->position++;

		if (ch->length > 0 && ch->position >= ch->length) {
			if (ch->loop_en && ch->loop_length > 0) {
				ch->position = ch->loop_start;
			} else {
				ch->position = ch->length;
				ch->playing = 0;
				break;
			}
		}
	}
}

/* ---- Mix ---- */

void
lud_audio_mix(int16_t *out, int nframes)
{
	int f, c;

	ma_spinlock_lock(&mix_lock);

	for (f = 0; f < nframes; f++) {
		int32_t left_acc = 0, right_acc = 0;
		int32_t left, right;

		for (c = 0; c < LUD_AUDIO_CHANNELS; c++) {
			channel_t *ch = &channels[c];
			int16_t sample;
			unsigned int steps;

			if (!ch->playing)
				continue;

			sample = fetch_sample(ch);
			left_acc  += ((int32_t)sample * ch->volume_l) >> 8;
			right_acc += ((int32_t)sample * ch->volume_r) >> 8;

			/* Advance position by pitch (8.8 fixed-point) */
			ch->frac += ch->pitch;
			steps = ch->frac >> 8;
			ch->frac &= 0xFF;

			if (steps > 0)
				advance_channel(ch, steps);
		}

		/* Apply master volume */
		left  = (left_acc  * master_vol_l) >> 8;
		right = (right_acc * master_vol_r) >> 8;

		/* Clip to int16 range */
		if (left > 32767)   left = 32767;
		if (left < -32768)  left = -32768;
		if (right > 32767)  right = 32767;
		if (right < -32768) right = -32768;

		out[f * 2 + 0] = (int16_t)left;
		out[f * 2 + 1] = (int16_t)right;
	}

	/* Tee output into capture buffer if active */
	if (cap_active) {
		if (cap_frames + (size_t)nframes > cap_cap) {
			size_t newcap = cap_cap ? cap_cap * 2 : 65536;
			int16_t *nb;
			while (newcap < cap_frames + (size_t)nframes)
				newcap *= 2;
			nb = realloc(cap_buf, newcap * 2 * sizeof(int16_t));
			if (nb) {
				cap_buf = nb;
				cap_cap = newcap;
			}
		}
		if (cap_frames + (size_t)nframes <= cap_cap) {
			memcpy(cap_buf + cap_frames * 2, out,
			       (size_t)nframes * 2 * sizeof(int16_t));
			cap_frames += (size_t)nframes;
		}
	}

	ma_spinlock_unlock(&mix_lock);
}

/* ---- Miniaudio callback ---- */

static void
audio_callback(ma_device *dev, void *output, const void *input,
               ma_uint32 frame_count)
{
	(void)dev;
	(void)input;
	lud_audio_mix((int16_t *)output, (int)frame_count);
}

/* ---- Public API ---- */

void
lud_audio_init(void)
{
	ma_device_config config;

	memset(channels, 0, sizeof(channels));
	master_vol_l = 255;
	master_vol_r = 255;
	mix_lock = 0;

	config = ma_device_config_init(ma_device_type_playback);
	config.playback.format   = ma_format_s16;
	config.playback.channels = 2;
	config.sampleRate        = LUD_AUDIO_SAMPLE_RATE;
	config.dataCallback      = audio_callback;

	if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
		lud_err("audio: failed to initialize device");
		return;
	}
	if (ma_device_start(&device) != MA_SUCCESS) {
		lud_err("audio: failed to start device");
		ma_device_uninit(&device);
		return;
	}
	device_inited = 1;
	lud_log("audio: device started (%d Hz, stereo, s16)",
	        LUD_AUDIO_SAMPLE_RATE);
}

void
lud_audio_shutdown(void)
{
	if (device_inited) {
		ma_device_uninit(&device);
		device_inited = 0;
	}
	memset(channels, 0, sizeof(channels));
}

void
lud_audio_play(int ch, const lud_audio_desc_t *desc)
{
	channel_t *c;

	if (ch < 0 || ch >= LUD_AUDIO_CHANNELS)
		return;

	c = &channels[ch];

	ma_spinlock_lock(&mix_lock);

	c->playing = 0;
	c->data = desc->data;
	c->length = desc->length;
	c->loop_start = desc->loop_start;
	c->loop_length = desc->loop_length;
	c->volume_l = desc->volume_l;
	c->volume_r = desc->volume_r;
	c->pitch = (unsigned int)desc->pitch;
	c->format = desc->format;
	c->loop_en = (desc->loop_length > 0) ? 1 : 0;
	c->position = 0;
	c->frac = 0;
	c->adpcm_pred = 0;
	c->adpcm_index = 0;
	c->playing = 1;

	ma_spinlock_unlock(&mix_lock);
}

void
lud_audio_stop(int ch)
{
	if (ch < 0 || ch >= LUD_AUDIO_CHANNELS)
		return;

	ma_spinlock_lock(&mix_lock);
	channels[ch].playing = 0;
	ma_spinlock_unlock(&mix_lock);
}

void
lud_audio_set_master(int left, int right)
{
	if (left < 0)   left = 0;
	if (left > 255)  left = 255;
	if (right < 0)  right = 0;
	if (right > 255) right = 255;
	master_vol_l = left;
	master_vol_r = right;
}

/* ---- Audio capture ---- */

void
lud_audio_capture_start(void)
{
	ma_spinlock_lock(&mix_lock);
	free(cap_buf);
	cap_buf = NULL;
	cap_frames = 0;
	cap_cap = 0;
	cap_active = 1;
	ma_spinlock_unlock(&mix_lock);
}

static void
le16(unsigned char *p, unsigned v)
{
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
}

static void
le32(unsigned char *p, unsigned v)
{
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
}

int
lud_audio_capture_stop(const char *wav_path)
{
	int16_t *buf;
	size_t frames;
	unsigned nr_samples, data_bytes, chunk_sz, byte_rate, block_align;
	unsigned sample_rate = LUD_AUDIO_SAMPLE_RATE;
	unsigned channels = 2;
	unsigned bits = 16;
	unsigned char header[44] = {
		'R','I','F','F', 0,0,0,0, 'W','A','V','E',
		'f','m','t',' ', 16,0,0,0, 1,0, 0,0, 0,0,0,0, 0,0,0,0, 0,0, 0,0,
		'd','a','t','a', 0,0,0,0,
	};
	unsigned i;
	FILE *f;

	/* Atomically stop capture and grab the buffer */
	ma_spinlock_lock(&mix_lock);
	cap_active = 0;
	buf = cap_buf;
	frames = cap_frames;
	cap_buf = NULL;
	cap_frames = 0;
	cap_cap = 0;
	ma_spinlock_unlock(&mix_lock);

	if (!buf || frames == 0) {
		free(buf);
		return -1;
	}

	nr_samples = (unsigned)(frames * 2);
	data_bytes = nr_samples * bits / 8;
	chunk_sz = sizeof(header) - 8 + data_bytes;
	byte_rate = sample_rate * channels * bits / 8;
	block_align = channels * bits / 8;

	le32(header + 4, chunk_sz);
	le16(header + 22, channels);
	le32(header + 24, sample_rate);
	le32(header + 28, byte_rate);
	le16(header + 32, block_align);
	le16(header + 34, bits);
	le32(header + 40, data_bytes);

	f = fopen(wav_path, "wb");
	if (!f) {
		free(buf);
		return -1;
	}
	fwrite(header, sizeof(header), 1, f);

	/* Write samples as little-endian int16 */
	for (i = 0; i < nr_samples; i++) {
		unsigned char le[2];
		int16_t s = buf[i];
		le[0] = (unsigned char)(s & 0xFF);
		le[1] = (unsigned char)((s >> 8) & 0xFF);
		fwrite(le, 2, 1, f);
	}

	fclose(f);
	free(buf);

	lud_log("audio: captured %s (%.2f seconds)",
	        wav_path, (double)frames / (double)sample_rate);
	return 0;
}
