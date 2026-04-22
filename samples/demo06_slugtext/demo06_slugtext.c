/*
 * demo06_slugtext — Old-school demo-style sine text scroller.
 *
 * Three simultaneous text scrollers over a parallax starfield:
 *   - Center: big per-character sine wave scroller
 *   - Top: small bouncing left-scroll
 *   - Bottom: small bouncing left-scroll (different phase)
 *
 * Each scroller uses a different Slug font. Colors cycle through
 * hue over time.
 *
 * Space: pause/unpause
 * 1/2/3: cycle font on scroller 1/2/3
 * Escape: quit
 *
 * Requires GLES3.
 */

#include "ludica.h"
#include "ludica_gfx.h"
#include "ludica_slug.h"
#include <math.h>
#include <string.h>

#define VW 960.0f
#define VH 540.0f

#define PI 3.14159265f

/* ------------------------------------------------------------------ */
/* HSV to RGB                                                          */
/* ------------------------------------------------------------------ */

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
	float depth;      /* 0.0 (far) to 1.0 (near) */
} star_t;

static star_t stars[NUM_STARS];

/* Current movement direction and target */
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
	/* Initial direction: moving left */
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
	/* Smoothly interpolate toward target direction */
	float lerp = 1.0f - expf(-2.0f * dt);
	star_dx += (star_target_dx - star_dx) * lerp;
	star_dy += (star_target_dy - star_dy) * lerp;

	/* Direction change timer */
	star_change_timer -= dt;
	if (star_change_timer <= 0.0f)
		pick_new_star_direction();

	float base_speed = 150.0f;
	for (int i = 0; i < NUM_STARS; i++) {
		float speed = base_speed * (0.3f + stars[i].depth * 0.7f);
		stars[i].x += star_dx * speed * dt;
		stars[i].y += star_dy * speed * dt;

		/* Wrap around */
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

		/* Streak length proportional to depth (near = longer) */
		float streak = 2.0f + d * 8.0f;
		float sx = stars[i].x;
		float sy = stars[i].y;
		float ex = sx - star_dx * streak;
		float ey = sy - star_dy * streak;

		/* Draw streak as a thin rect aligned to direction */
		float dx = ex - sx;
		float dy = ey - sy;
		float len = sqrtf(dx * dx + dy * dy);
		if (len < 0.5f) len = 0.5f;

		/* Perpendicular for thickness */
		float thickness = 0.5f + d * 1.0f;
		float nx = -dy / len * thickness * 0.5f;
		float ny =  dx / len * thickness * 0.5f;

		/* Use rect for simplicity: bounding box of the streak */
		float min_x = sx < ex ? sx : ex;
		float min_y = sy < ey ? sy : ey;
		float w = fabsf(dx);
		float h = fabsf(dy);
		if (w < thickness) w = thickness;
		if (h < thickness) h = thickness;

		(void)nx; (void)ny;
		lud_sprite_rect(min_x - thickness * 0.5f, min_y - thickness * 0.5f,
		                w + thickness, h + thickness,
		                brightness, brightness, brightness * 1.1f, brightness);
	}
}

/* ------------------------------------------------------------------ */
/* Scrollers                                                           */
/* ------------------------------------------------------------------ */

#define NUM_FONTS 3
#define NUM_SCROLLERS 3

static lud_slug_font_t fonts[NUM_FONTS];
static const char *font_paths[NUM_FONTS] = {
	"assets/fonts/Aileron-Regular.slugfont",
	"assets/fonts/Kenney-Future.slugfont",
	"assets/fonts/Unispace-Regular.slugfont",
};
static const char *font_names[NUM_FONTS] = {
	"Aileron", "Kenney Future", "Unispace"
};

static int scroller_font[NUM_SCROLLERS] = { 0, 1, 2 };

static const char *big_text =
	"    LUDICA GPU VECTOR FONT DEMO    "
	"SLUG ALGORITHM BY ERIC LENGYEL    "
	"RESOLUTION INDEPENDENT TEXT RENDERING    "
	"NO TEXTURE ATLASES - PURE MATH    "
	"GREETINGS TO ALL CODERS AND DEMOSCENERS    ";

static const char *top_text =
	"    Three CC0 fonts: Aileron + Kenney Future + Unispace    "
	"Press 1/2/3 to cycle fonts -- Space to pause    ";

static const char *bot_text =
	"    Quadratic Bezier curves evaluated per-pixel in the fragment shader    "
	"Winding number via ray casting with band acceleration    ";

static float scroll_x_big;
static float scroll_x_top;
static float scroll_x_bot;
static float time_acc;
static int paused;

/* Measure text once per font assignment so we know wrap points */
static float big_text_width;
static float top_text_width;
static float bot_text_width;

static void
recalc_widths(void)
{
	big_text_width = lud_slug_text_width(fonts[scroller_font[0]], 72.0f, big_text);
	top_text_width = lud_slug_text_width(fonts[scroller_font[1]], 28.0f, top_text);
	bot_text_width = lud_slug_text_width(fonts[scroller_font[2]], 28.0f, bot_text);
}

static void
draw_big_scroller(void)
{
	lud_slug_font_t f = fonts[scroller_font[0]];
	float size = 72.0f;
	float center_y = VH * 0.48f;
	float amplitude = 50.0f;
	float phase_speed = 3.0f;
	float char_spread = 0.25f;
	float hue_base = fmodf(time_acc * 60.0f, 360.0f);

	/* Draw character by character for per-char sine */
	float cx = -scroll_x_big;
	int len = (int)strlen(big_text);
	char buf[2] = { 0, 0 };

	for (int i = 0; i < len; i++) {
		float cw = lud_slug_text_width(f, size, " ");
		buf[0] = big_text[i];
		cw = lud_slug_text_width(f, size, buf);

		/* Skip if offscreen */
		if (cx + cw < -size || cx > VW + size) {
			cx += cw;
			continue;
		}

		float y_off = amplitude * sinf(time_acc * phase_speed + (float)i * char_spread);
		float char_hue = fmodf(hue_base + (float)i * 8.0f, 360.0f);
		float r, g, b;
		hsv2rgb(char_hue, 0.8f, 1.0f, &r, &g, &b);

		lud_pen_t pen = { cx, center_y + y_off };
		lud_slug_draw(f, &pen, size, r, g, b, 1.0f, buf);
		cx += cw;
	}
}

static void
draw_small_scroller(int idx, float y_center, float bounce_freq,
                    float bounce_amp, const char *text,
                    float scroll_x, float hue_offset)
{
	lud_slug_font_t f = fonts[scroller_font[idx]];
	float size = 28.0f;
	float y = y_center + bounce_amp * sinf(time_acc * bounce_freq);
	float hue = fmodf(time_acc * 45.0f + hue_offset, 360.0f);
	float r, g, b;
	hsv2rgb(hue, 0.7f, 0.95f, &r, &g, &b);

	lud_pen_t pen = { -scroll_x, y };
	lud_slug_draw(f, &pen, size, r, g, b, 1.0f, text);
}

/* ------------------------------------------------------------------ */
/* App callbacks                                                       */
/* ------------------------------------------------------------------ */

static void
init(void)
{
	for (int i = 0; i < NUM_FONTS; i++) {
		fonts[i] = lud_load_slug_font(font_paths[i]);
		if (fonts[i].id == 0) {
			/* Try with executable-relative path */
		}
	}

	init_stars();
	scroll_x_big = 0.0f;
	scroll_x_top = 0.0f;
	scroll_x_bot = 0.0f;
	time_acc = 0.0f;
	paused = 0;
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
	case LUD_KEY_1:
		scroller_font[0] = (scroller_font[0] + 1) % NUM_FONTS;
		recalc_widths();
		return 1;
	case LUD_KEY_2:
		scroller_font[1] = (scroller_font[1] + 1) % NUM_FONTS;
		recalc_widths();
		return 1;
	case LUD_KEY_3:
		scroller_font[2] = (scroller_font[2] + 1) % NUM_FONTS;
		recalc_widths();
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

		/* Update scroll positions */
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

	/* Starfield (sprite batch) */
	lud_sprite_begin(0, 0, VW, VH);
	draw_stars();
	lud_sprite_end();

	/* Text (Slug) */
	lud_slug_begin(0, 0, VW, VH);
	draw_small_scroller(1, 80.0f, 1.7f, 25.0f, top_text, scroll_x_top, 120.0f);
	draw_big_scroller();
	draw_small_scroller(2, VH - 80.0f, 2.3f, 20.0f, bot_text, scroll_x_bot, 240.0f);

	/* HUD: font names and controls */
	{
		char buf[128];
		int n = 0;
		const char *p = "1:";
		while (*p) buf[n++] = *p++;
		p = font_names[scroller_font[0]];
		while (*p) buf[n++] = *p++;
		p = "  2:";
		while (*p) buf[n++] = *p++;
		p = font_names[scroller_font[1]];
		while (*p) buf[n++] = *p++;
		p = "  3:";
		while (*p) buf[n++] = *p++;
		p = font_names[scroller_font[2]];
		while (*p) buf[n++] = *p++;
		buf[n] = '\0';
		lud_pen_t hud_pen = { 8.0f, VH - 16.0f };
		lud_slug_draw(fonts[2], &hud_pen, 14.0f,
		              0.5f, 0.5f, 0.5f, 0.7f, buf);
	}
	if (paused) {
		lud_pen_t pause_pen = { VW * 0.5f - 40.0f, VH * 0.5f - 40.0f };
		lud_slug_draw(fonts[0], &pause_pen, 48.0f,
		              1.0f, 1.0f, 1.0f, 0.8f, "PAUSED");
	}

	lud_slug_end();
}

static void
cleanup(void)
{
	for (int i = 0; i < NUM_FONTS; i++)
		lud_destroy_slug_font(fonts[i]);
}

int
main(int argc, char **argv)
{
	return lud_run(&(lud_desc_t){
		.app_name = "demo06 — slug text",
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
