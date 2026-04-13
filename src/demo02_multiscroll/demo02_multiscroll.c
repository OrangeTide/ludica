/*
 * demo02_multiscroll — Multi-layer parallax scrolling with tilemap.
 *
 * Demonstrates ludica's sprite batch, image loading, and 2D rendering.
 * Arrow keys to scroll, Escape to quit.
 */

#include "ludica.h"
#include "ludica_gfx.h"
#include <math.h>
#include <string.h>

#define VIRTUAL_W  320
#define VIRTUAL_H  180
#define TILE_SIZE  24
#define TILESET_COLS 21
#define MAP_W      80
#define MAP_H      8

/* Tile indices matching the Oak Woods tileset layout (21 columns) */
#define TILE_TL    0    /* top-left corner */
#define TILE_TC    1    /* top center (grass) */
#define TILE_TC2   2    /* top center alt */
#define TILE_TR    3    /* top-right corner */
#define TILE_ML   21    /* middle left edge */
#define TILE_MC   22    /* middle center (dirt fill) */
#define TILE_MR   24    /* middle right edge */

static lud_texture_t bg[3];
static lud_texture_t tileset;
static int tilemap[MAP_H][MAP_W];
static float cam_x;
static int scroll_left, scroll_right;

static void
tilemap_init(void)
{
	int x;

	/* Clear all tiles to empty (-1) */
	memset(tilemap, 0xff, sizeof(tilemap));

	/* Ground: rows 6-7 across the entire map */
	for (x = 0; x < MAP_W; x++) {
		tilemap[6][x] = (x % 2 == 0) ? TILE_TC : TILE_TC2;
		tilemap[7][x] = TILE_MC;
	}

	/* Elevated section: a step up at columns 30-39 */
	/* Right edge of ground before step */
	tilemap[6][29] = TILE_TR;
	tilemap[7][29] = TILE_MR;

	/* Step body (rows 6-7 become solid wall) */
	tilemap[6][30] = TILE_ML;
	tilemap[7][30] = TILE_ML;
	for (x = 31; x < 39; x++) {
		tilemap[6][x] = TILE_MC;
		tilemap[7][x] = TILE_MC;
	}
	tilemap[6][39] = TILE_MR;
	tilemap[7][39] = TILE_MR;

	/* Left edge of ground after step */
	tilemap[6][40] = TILE_TL;
	tilemap[7][40] = TILE_ML;

	/* Upper platform surface (row 5) */
	tilemap[5][30] = TILE_TL;
	for (x = 31; x < 39; x++)
		tilemap[5][x] = (x % 2 == 0) ? TILE_TC : TILE_TC2;
	tilemap[5][39] = TILE_TR;

	/* Second elevated section at columns 55-65, row 4 */
	tilemap[6][54] = TILE_TR;
	tilemap[7][54] = TILE_MR;

	tilemap[6][55] = TILE_ML;
	tilemap[7][55] = TILE_ML;
	for (x = 56; x < 65; x++) {
		tilemap[6][x] = TILE_MC;
		tilemap[7][x] = TILE_MC;
	}
	tilemap[6][65] = TILE_MR;
	tilemap[7][65] = TILE_MR;

	tilemap[6][66] = TILE_TL;
	tilemap[7][66] = TILE_ML;

	/* Row 5 fill */
	tilemap[5][55] = TILE_ML;
	for (x = 56; x < 65; x++)
		tilemap[5][x] = TILE_MC;
	tilemap[5][65] = TILE_MR;

	/* Upper platform surface (row 4) */
	tilemap[4][55] = TILE_TL;
	for (x = 56; x < 65; x++)
		tilemap[4][x] = (x % 2 == 0) ? TILE_TC : TILE_TC2;
	tilemap[4][65] = TILE_TR;
}

static void
init(void)
{
	bg[0] = lud_load_texture("src/demo02_multiscroll/assets/background_layer_1.png",
	                            LUD_FILTER_NEAREST, LUD_FILTER_NEAREST);
	bg[1] = lud_load_texture("src/demo02_multiscroll/assets/background_layer_2.png",
	                            LUD_FILTER_NEAREST, LUD_FILTER_NEAREST);
	bg[2] = lud_load_texture("src/demo02_multiscroll/assets/background_layer_3.png",
	                            LUD_FILTER_NEAREST, LUD_FILTER_NEAREST);
	tileset = lud_load_texture("src/demo02_multiscroll/assets/oak_woods_tileset.png",
	                              LUD_FILTER_NEAREST, LUD_FILTER_NEAREST);

	tilemap_init();
	cam_x = 0.0f;
}

static void
draw_parallax(lud_texture_t tex, float rate)
{
	float tw = (float)lud_texture_width(tex);
	float th = (float)lud_texture_height(tex);
	float offset;

	if (tw <= 0.0f)
		return;

	offset = fmodf(cam_x * rate, tw);
	if (offset < 0.0f)
		offset += tw;

	lud_sprite_draw(tex, -offset, 0, tw, th, 0, 0, 0, 0);
	lud_sprite_draw(tex, tw - offset, 0, tw, th, 0, 0, 0, 0);
}

static void
draw_tile(int idx, float wx, float wy)
{
	int col = idx % TILESET_COLS;
	int row = idx / TILESET_COLS;

	lud_sprite_draw(tileset,
	                   wx, wy, TILE_SIZE, TILE_SIZE,
	                   (float)(col * TILE_SIZE), (float)(row * TILE_SIZE),
	                   TILE_SIZE, TILE_SIZE);
}

static void
draw_tilemap_visible(void)
{
	int start_col, end_col, x, y;

	start_col = (int)(cam_x / TILE_SIZE);
	if (start_col < 0) start_col = 0;
	end_col = (int)((cam_x + VIRTUAL_W) / TILE_SIZE) + 1;
	if (end_col > MAP_W) end_col = MAP_W;

	for (y = 0; y < MAP_H; y++) {
		for (x = start_col; x < end_col; x++) {
			if (tilemap[y][x] >= 0)
				draw_tile(tilemap[y][x],
				          (float)(x * TILE_SIZE),
				          (float)(y * TILE_SIZE));
		}
	}
}

static int
on_event(const lud_event_t *ev)
{
	switch (ev->type) {
	case LUD_EV_KEY_DOWN:
		if (ev->key.keycode == LUD_KEY_ESCAPE)
			lud_quit();
		else if (ev->key.keycode == LUD_KEY_LEFT)
			scroll_left = 1;
		else if (ev->key.keycode == LUD_KEY_RIGHT)
			scroll_right = 1;
		return 1;
	case LUD_EV_KEY_UP:
		if (ev->key.keycode == LUD_KEY_LEFT)
			scroll_left = 0;
		else if (ev->key.keycode == LUD_KEY_RIGHT)
			scroll_right = 0;
		return 1;
	default:
		return 0;
	}
}

static void
frame(float dt)
{
	float scroll_speed = 120.0f;
	float auto_speed = 30.0f;
	float max_cam_x;

	/* Update camera from input, auto-scroll when idle */
	if (scroll_left)
		cam_x -= scroll_speed * dt;
	else if (scroll_right)
		cam_x += scroll_speed * dt;
	else
		cam_x += auto_speed * dt;

	/* Clamp camera */
	max_cam_x = (float)(MAP_W * TILE_SIZE) - VIRTUAL_W;
	if (cam_x < 0.0f) cam_x = 0.0f;
	if (cam_x > max_cam_x) cam_x = max_cam_x;

	lud_viewport(0, 0, lud_width(), lud_height());
	lud_clear(0.0f, 0.0f, 0.0f, 1.0f);

	/* Draw parallax backgrounds (fixed camera) */
	lud_sprite_begin(0, 0, VIRTUAL_W, VIRTUAL_H);
	draw_parallax(bg[0], 0.0f);
	draw_parallax(bg[1], 0.3f);
	draw_parallax(bg[2], 0.6f);
	lud_sprite_end();

	/* Draw tilemap (world camera) */
	lud_sprite_begin(cam_x, 0, VIRTUAL_W, VIRTUAL_H);
	draw_tilemap_visible();
	lud_sprite_end();
}

static void
cleanup(void)
{
	int i;
	for (i = 0; i < 3; i++)
		lud_destroy_texture(bg[i]);
	lud_destroy_texture(tileset);
}

int
main(void)
{
	return lud_run(&(lud_desc_t){
		.app_name = "demo02 — parallax multiscroll",
		.width = 960,
		.height = 540,
		.resizable = 1,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.event = on_event,
	});
}
