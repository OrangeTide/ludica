/*
 * demo05_audio -- Multi-channel audio demo
 *
 * Generates sine (PCM16) and square (PCM8) waveforms and plays a C major
 * chord as a rising arpeggio across 4 channels with stereo panning.
 * Visual feedback via colored VU bars.  Adapted from the Triton console
 * audio demo.
 */

#include <ludica.h>
#include <ludica_gfx.h>
#include <ludica_font.h>
#include <string.h>

/* ---- Waveform data ----------------------------------------------------- */

#define WAVE_LEN 256

static int16_t sine_wave[WAVE_LEN];
static int8_t  square_wave[WAVE_LEN];

/*
 * Sine table: 256 entries, Q15 (+-32767).  One full cycle.
 */
static const int16_t sin_table[WAVE_LEN] = {
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
      6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
     12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
     23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
     27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
     32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
     32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
     30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
     27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
     18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
     12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
         0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,
};

static void
generate_waveforms(void)
{
    memcpy(sine_wave, sin_table, sizeof(sine_wave));

    for (int i = 0; i < WAVE_LEN; i++)
        square_wave[i] = (i < WAVE_LEN / 2) ? 100 : -100;
}

/* ---- Note frequencies (8.8 fixed-point pitch values) ------------------- */

/*
 * With a 256-sample single cycle at 44100 Hz, base frequency is
 * 44100 / 256 = 172.27 Hz.  pitch = freq_hz / 172.27 * 256.
 */
#define NOTE_C4  389    /* 261.63 Hz */
#define NOTE_E4  490    /* 329.63 Hz */
#define NOTE_G4  583    /* 392.00 Hz */
#define NOTE_C5  778    /* 523.25 Hz */

/* ---- Channel definitions ----------------------------------------------- */

#define NUM_VOICES 4

struct voice {
    const char *label;
    const void *data;
    unsigned int length;
    int pitch;
    int vol_l, vol_r;
    enum lud_audio_fmt format;
    float r, g, b;          /* bar color */
};

static const struct voice voices[NUM_VOICES] = {
    { "C4 sine",    NULL, WAVE_LEN, NOTE_C4, 96, 32, LUD_AUDIO_PCM16,
      1.0f, 0.2f, 0.2f },   /* red, panned left */
    { "E4 sine",    NULL, WAVE_LEN, NOTE_E4, 72, 56, LUD_AUDIO_PCM16,
      0.2f, 1.0f, 0.2f },   /* green, center-left */
    { "G4 sine",    NULL, WAVE_LEN, NOTE_G4, 56, 72, LUD_AUDIO_PCM16,
      0.2f, 0.4f, 1.0f },   /* blue, center-right */
    { "C5 square",  NULL, WAVE_LEN, NOTE_C5, 32, 96, LUD_AUDIO_PCM8,
      1.0f, 1.0f, 0.2f },   /* yellow, panned right */
};

/* ---- Demo state -------------------------------------------------------- */

enum phase {
    PHASE_BEAT1,    /* C4 alone */
    PHASE_BEAT2,    /* + E4 */
    PHASE_BEAT3,    /* + G4 */
    PHASE_BEAT4,    /* + C5 square */
    PHASE_FADE3,    /* drop C5 */
    PHASE_FADE2,    /* drop G4 */
    PHASE_FADE1,    /* drop E4 */
    PHASE_FADE0,    /* drop C4 */
    PHASE_DONE,
    NUM_PHASES
};

static const float phase_duration[] = {
    [PHASE_BEAT1] = 1.0f,
    [PHASE_BEAT2] = 1.0f,
    [PHASE_BEAT3] = 1.0f,
    [PHASE_BEAT4] = 2.0f,
    [PHASE_FADE3] = 0.5f,
    [PHASE_FADE2] = 0.5f,
    [PHASE_FADE1] = 0.5f,
    [PHASE_FADE0] = 0.5f,
    [PHASE_DONE]  = 0.0f,
};

/* How many voices are active in each phase */
static const int phase_active[] = {
    [PHASE_BEAT1] = 1,
    [PHASE_BEAT2] = 2,
    [PHASE_BEAT3] = 3,
    [PHASE_BEAT4] = 4,
    [PHASE_FADE3] = 3,
    [PHASE_FADE2] = 2,
    [PHASE_FADE1] = 1,
    [PHASE_FADE0] = 0,
    [PHASE_DONE]  = 0,
};

static const char *phase_label[] = {
    [PHASE_BEAT1] = "C4",
    [PHASE_BEAT2] = "C4 + E4",
    [PHASE_BEAT3] = "C4 + E4 + G4",
    [PHASE_BEAT4] = "Full chord + C5 square",
    [PHASE_FADE3] = "Fade: drop C5",
    [PHASE_FADE2] = "Fade: drop G4",
    [PHASE_FADE1] = "Fade: drop E4",
    [PHASE_FADE0] = "Silence",
    [PHASE_DONE]  = "Demo complete - press R to restart",
};

static int current_phase;
static float phase_timer;
static int active_mask;     /* bitmask of playing channels */
static lud_font_t font;

static int screen_w, screen_h;

/* ---- Audio helpers ----------------------------------------------------- */

static void
play_voice(int ch)
{
    const struct voice *v = &voices[ch];

    lud_audio_play(ch, &(lud_audio_desc_t){
        .data        = (ch == 3) ? (const void *)square_wave
                                 : (const void *)sine_wave,
        .length      = v->length,
        .loop_start  = 0,
        .loop_length = v->length,
        .volume_l    = v->vol_l,
        .volume_r    = v->vol_r,
        .pitch       = v->pitch,
        .format      = v->format,
    });
    active_mask |= (1 << ch);
}

static void
stop_voice(int ch)
{
    lud_audio_stop(ch);
    active_mask &= ~(1 << ch);
}

/* ---- Phase transitions ------------------------------------------------- */

static void
enter_phase(int phase)
{
    current_phase = phase;
    phase_timer = 0.0f;

    int target = phase_active[phase];
    int current_count = 0;
    for (int i = 0; i < NUM_VOICES; i++)
        if (active_mask & (1 << i))
            current_count++;

    /* Start voices to reach target count */
    while (current_count < target) {
        play_voice(current_count);
        current_count++;
    }
    /* Stop voices to reach target count (from top) */
    while (current_count > target) {
        current_count--;
        stop_voice(current_count);
    }
}

static void
restart_demo(void)
{
    for (int i = 0; i < NUM_VOICES; i++)
        lud_audio_stop(i);
    active_mask = 0;
    enter_phase(PHASE_BEAT1);
}

/* ---- Rendering --------------------------------------------------------- */

static void
draw_vu_display(void)
{
    float bar_x = 120.0f;
    float bar_y = 160.0f;
    float bar_h = 40.0f;
    float gap = 60.0f;
    float max_w = (float)(screen_w - 160);
    float bar_widths[NUM_VOICES] = {
        max_w * 0.80f, max_w * 0.67f, max_w * 0.75f, max_w * 0.53f
    };

    for (int i = 0; i < NUM_VOICES; i++) {
        float y = bar_y + (float)i * gap;
        const struct voice *v = &voices[i];
        int on = (active_mask & (1 << i)) != 0;
        float w = on ? bar_widths[i] : 40.0f;
        float r = on ? v->r : 0.25f;
        float g = on ? v->g : 0.25f;
        float b = on ? v->b : 0.25f;

        lud_sprite_rect(bar_x, y, w, bar_h, r, g, b, 1.0f);

        /* Label */
        lud_draw_text(font, bar_x + w + 10.0f, y + 12.0f, 2, v->label);
    }
}

/* ---- Callbacks --------------------------------------------------------- */

static void
init(void)
{
    generate_waveforms();
    lud_audio_init();
    lud_audio_set_master(200, 200);
    font = lud_make_default_font();
    screen_w = lud_width();
    screen_h = lud_height();
    enter_phase(PHASE_BEAT1);
}

static void
frame(float dt)
{
    /* Advance phase */
    if (current_phase < PHASE_DONE) {
        phase_timer += dt;
        if (phase_timer >= phase_duration[current_phase])
            enter_phase(current_phase + 1);
    }

    /* Restart on R */
    if (lud_key_down(LUD_KEY_R) && current_phase == PHASE_DONE)
        restart_demo();

    /* Draw */
    lud_clear(0.06f, 0.06f, 0.06f, 1.0f);

    lud_sprite_begin(0, 0, screen_w, screen_h);

    /* Title */
    lud_draw_text(font, 20, 20, 3, "Audio Demo");
    lud_draw_text(font, 20, 60, 2, phase_label[current_phase]);

    /* Progress bar for current phase */
    if (current_phase < PHASE_DONE) {
        float frac = phase_timer / phase_duration[current_phase];
        if (frac > 1.0f) frac = 1.0f;
        float pw = (float)(screen_w - 40) * frac;
        lud_sprite_rect(20.0f, 100.0f, pw, 8.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    }

    /* VU bars */
    draw_vu_display();

    /* Instructions */
    lud_draw_text(font, 20, screen_h - 40, 1, "R = restart    ESC = quit");

    lud_sprite_end();
}

static int
on_event(const lud_event_t *ev)
{
    if (ev->type == LUD_EV_KEY_DOWN && ev->key.keycode == LUD_KEY_ESCAPE)
        return 1;
    if (ev->type == LUD_EV_RESIZED) {
        screen_w = ev->resize.width;
        screen_h = ev->resize.height;
    }
    return 0;
}

static void
cleanup(void)
{
    for (int i = 0; i < NUM_VOICES; i++)
        lud_audio_stop(i);
    lud_audio_shutdown();
}

/* ---- Entry point ------------------------------------------------------- */

int
main(int argc, char **argv)
{
    return lud_run(&(lud_desc_t){
        .app_name = "Audio Demo",
        .width    = 800,
        .height   = 600,
        .argc     = argc,
        .argv     = (const char *const *)argv,
        .init     = init,
        .frame    = frame,
        .cleanup  = cleanup,
        .event    = on_event,
    });
}
