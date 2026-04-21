/*
 * demo07_vfont — Vector font demo using the unified vfont API.
 *
 * Sine-wave text scroller over a parallax starfield, demonstrating
 * lud_vfont_* which selects Slug (GLES3) or SDF (GLES2) automatically.
 *
 * Space: pause/unpause
 * Escape: quit
 */

#include "ludica.h"
#include "ludica_gfx.h"
#include "ludica_vfont.h"
#include <math.h>
#include <string.h>

#define VW 960.0f
#define VH 540.0f

#define PI 3.14159265f

static void
hsv2rgb(float h, float s, float v, float *r, float *g, float *b)
{
	h = fmodf(h, 360.0f);
	if (h < 0.0f) h += 360.0f;
	float c = v * s;
	float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
	float m = v - c;
	float r1, g1, b1;
	if      (h < 60.0f)  { r1 = c; g1 = x; b1 = 0; }
	else if (h < 120.0f) { r1 = x; g1 = c; b1 = 0; }
	else if (h < 180.0f) { r1 = 0; g1 = c; b1 = x; }
	else if (h < 240.0f) { r1 = 0; g1 = x; b1 = c; }
	else if (h < 300.0f) { r1 = x; g1 = 0; b1 = c; }
	else                  { r1 = c; g1 = 0; b1 = x; }
	*r = r1 + m;
	*g = g1 + m;
	*b = b1 + m;
}

/* ------------------------------------------------------------------ */
/* Simple xorshift32 PRNG                                              */
/* ------------------------------------------------------------------ */

static unsigned rng_state = 0xDEADBEEF;

static unsigned
rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static float
rng_float(void)
{
	return (float)(rng() & 0xFFFFFF) / (float)0xFFFFFF;
}

/* ------------------------------------------------------------------ */
/* Starfield                                                           */
/* ------------------------------------------------------------------ */

#define NUM_STARS 200

typedef struct {
	float x, y;
	float depth;
} star_t;

static star_t stars[NUM_STARS];

static float star_dx, star_dy;
static float star_target_dx, star_target_dy;
static float star_change_timer;

static void
init_stars(void)
{
	for (int i = 0; i < NUM_STARS; i++) {
		stars[i].x = rng_float() * VW;
		stars[i].y = rng_float() * VH;
		stars[i].depth = 0.1f + rng_float() * 0.9f;
	}
	star_dx = -1.0f;
	star_dy = 0.0f;
	star_target_dx = star_dx;
	star_target_dy = star_dy;
	star_change_timer = 3.0f + rng_float() * 5.0f;
}

static void
pick_new_star_direction(void)
{
	float angle = rng_float() * 2.0f * PI;
	star_target_dx = cosf(angle);
	star_target_dy = sinf(angle);
	star_change_timer = 3.0f + rng_float() * 5.0f;
}

static void
update_stars(float dt)
{
	float lerp = 1.0f - expf(-2.0f * dt);
	star_dx += (star_target_dx - star_dx) * lerp;
	star_dy += (star_target_dy - star_dy) * lerp;

	star_change_timer -= dt;
	if (star_change_timer <= 0.0f)
		pick_new_star_direction();

	float base_speed = 150.0f;
	for (int i = 0; i < NUM_STARS; i++) {
		float speed = base_speed * (0.3f + stars[i].depth * 0.7f);
		stars[i].x += star_dx * speed * dt;
		stars[i].y += star_dy * speed * dt;

		if (stars[i].x < -20.0f)    stars[i].x += VW + 40.0f;
		if (stars[i].x > VW + 20.0f) stars[i].x -= VW + 40.0f;
		if (stars[i].y < -20.0f)    stars[i].y += VH + 40.0f;
		if (stars[i].y > VH + 20.0f) stars[i].y -= VH + 40.0f;
	}
}

static void
draw_stars(void)
{
	for (int i = 0; i < NUM_STARS; i++) {
		float d = stars[i].depth;
		float brightness = 0.3f + d * 0.7f;

		float streak = 2.0f + d * 8.0f;
		float sx = stars[i].x;
		float sy = stars[i].y;
		float ex = sx - star_dx * streak;
		float ey = sy - star_dy * streak;

		float dx = ex - sx;
		float dy = ey - sy;
		float len = sqrtf(dx * dx + dy * dy);
		if (len < 0.5f) len = 0.5f;

		float thickness = 0.5f + d * 1.0f;

		float min_x = sx < ex ? sx : ex;
		float min_y = sy < ey ? sy : ey;
		float w = fabsf(dx);
		float h = fabsf(dy);
		if (w < thickness) w = thickness;
		if (h < thickness) h = thickness;

		lud_sprite_rect(min_x - thickness * 0.5f, min_y - thickness * 0.5f,
		                w + thickness, h + thickness,
		                brightness, brightness, brightness * 1.1f, brightness);
	}
}

/* ------------------------------------------------------------------ */
/* Scrollers                                                           */
/* ------------------------------------------------------------------ */

static lud_vfont_t font;

static const char *big_text =
	"    LUDICA VECTOR FONT DEMO    "
	"UNIFIED API: SLUG ON GLES3, SDF ON GLES2    "
	"RESOLUTION INDEPENDENT TEXT RENDERING    "
	"GREETINGS TO ALL CODERS AND DEMOSCENERS    ";

static const char *top_text =
	"    DejaVu Sans LGC -- Bitstream Vera license    "
	"Press Space to pause    ";

static const char *bot_text =
	"    lud_vfont_begin / lud_vfont_draw / lud_vfont_end    "
	"One API for both backends -- font files auto-selected at runtime    ";

static float scroll_x_big;
static float scroll_x_top;
static float scroll_x_bot;
static float time_acc;
static int paused;

static float big_text_width;
static float top_text_width;
static float bot_text_width;

static void
recalc_widths(void)
{
	big_text_width = lud_vfont_text_width(font, 72.0f, big_text);
	top_text_width = lud_vfont_text_width(font, 28.0f, top_text);
	bot_text_width = lud_vfont_text_width(font, 28.0f, bot_text);
}

static void
draw_big_scroller(void)
{
	float size = 72.0f;
	float center_y = VH * 0.48f;
	float amplitude = 50.0f;
	float phase_speed = 3.0f;
	float char_spread = 0.25f;
	float hue_base = fmodf(time_acc * 60.0f, 360.0f);

	int len = (int)strlen(big_text);
	char buf[2] = { 0, 0 };

	for (int pass = 0; pass < 2; pass++) {
		float cx = -scroll_x_big + pass * big_text_width;

		for (int i = 0; i < len; i++) {
			buf[0] = big_text[i];
			float cw = lud_vfont_text_width(font, size, buf);

			if (cx + cw < -size || cx > VW + size) {
				cx += cw;
				continue;
			}

			float y_off = amplitude * sinf(time_acc * phase_speed + (float)i * char_spread);
			float char_hue = fmodf(hue_base + (float)i * 8.0f, 360.0f);
			float r, g, b;
			hsv2rgb(char_hue, 0.8f, 1.0f, &r, &g, &b);

			lud_vfont_draw(font, cx, center_y + y_off, size, r, g, b, 1.0f, buf);
			cx += cw;
		}
	}
}

static void
draw_small_scroller(float y_center, float bounce_freq, float bounce_amp,
                    const char *text, float text_width,
                    float scroll_x, float hue_offset)
{
	float size = 28.0f;
	float y = y_center + bounce_amp * sinf(time_acc * bounce_freq);
	float hue = fmodf(time_acc * 45.0f + hue_offset, 360.0f);
	float r, g, b;
	hsv2rgb(hue, 0.7f, 0.95f, &r, &g, &b);

	lud_vfont_draw(font, -scroll_x, y, size, r, g, b, 1.0f, text);
	if (text_width > 0.0f)
		lud_vfont_draw(font, -scroll_x + text_width, y,
		               size, r, g, b, 1.0f, text);
}

/* ------------------------------------------------------------------ */
/* App callbacks                                                       */
/* ------------------------------------------------------------------ */

static void
init(void)
{
	font = lud_load_vfont("assets/fonts/dejavu-sans");
	init_stars();
	recalc_widths();
}

static int
on_event(const lud_event_t *ev)
{
	if (ev->type != LUD_EV_KEY_DOWN)
		return 0;

	switch (ev->key.keycode) {
	case LUD_KEY_ESCAPE:
		lud_quit();
		return 1;
	case LUD_KEY_SPACE:
		paused = !paused;
		return 1;
	default:
		break;
	}
	return 0;
}

static void
frame(float dt)
{
	if (!paused) {
		time_acc += dt;

		float big_speed = 120.0f;
		float small_speed = 100.0f;

		scroll_x_big += big_speed * dt;
		if (big_text_width > 0.0f && scroll_x_big > big_text_width)
			scroll_x_big -= big_text_width;

		scroll_x_top += small_speed * dt;
		if (top_text_width > 0.0f && scroll_x_top > top_text_width)
			scroll_x_top -= top_text_width;

		scroll_x_bot += small_speed * 1.15f * dt;
		if (bot_text_width > 0.0f && scroll_x_bot > bot_text_width)
			scroll_x_bot -= bot_text_width;

		update_stars(dt);
	}

	lud_viewport(0, 0, lud_width(), lud_height());
	lud_clear(0.02f, 0.02f, 0.06f, 1.0f);

	/* Starfield */
	lud_sprite_begin(0, 0, VW, VH);
	draw_stars();
	lud_sprite_end();

	/* Text (vfont — auto-selects Slug or SDF backend) */
	lud_vfont_begin(0, 0, VW, VH);
	draw_small_scroller(80.0f, 1.7f, 25.0f, top_text, top_text_width,
	                    scroll_x_top, 120.0f);
	draw_big_scroller();
	draw_small_scroller(VH - 80.0f, 2.3f, 20.0f, bot_text, bot_text_width,
	                    scroll_x_bot, 240.0f);

	if (paused)
		lud_vfont_draw(font, VW * 0.5f - 100.0f, VH * 0.5f - 20.0f, 48.0f,
		               1.0f, 1.0f, 1.0f, 0.8f, "PAUSED");

	/* Backend indicator */
	{
		const char *backend = lud_gles_version() >= 3 ? "Slug (GLES3)" : "SDF (GLES2)";
		lud_vfont_draw(font, 8.0f, VH - 16.0f, 14.0f,
		               0.5f, 0.5f, 0.5f, 0.7f, backend);
	}

	lud_vfont_end();
}

static void
cleanup(void)
{
	lud_destroy_vfont(font);
}

int
main(int argc, char **argv)
{
	return lud_run(&(lud_desc_t){
		.app_name = "demo07 \xe2\x80\x94 vfont",
		.width = 960,
		.height = 540,
                .gles_version = 3,
		.resizable = 1,
		.argc = argc,
		.argv = argv,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.event = on_event,
	});
}
