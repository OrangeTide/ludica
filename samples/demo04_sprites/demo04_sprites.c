/* demo04_sprites — Sprite animation + tile collision platformer.
 *
 * Uses the Oak Woods tileset (brullov) with a 56x56 character spritesheet,
 * parallax scrolling backgrounds, and AABB tile collision. */

#include <ludica.h>
#include <ludica_gfx.h>
#include <ludica_anim.h>
#include <stddef.h>

#define VIRTUAL_W 320
#define VIRTUAL_H 180
#define TILE_SIZE 24
#define MAP_W 80
#define MAP_H 8
#define CHAR_FRAME_W 56
#define CHAR_FRAME_H 56
#define CHAR_COLS 8

#define GRAVITY 800.0f
#define MOVE_SPEED 100.0f
#define JUMP_VEL (-280.0f)

/* Player AABB body box — 16x28, offset from sprite top-left */
#define BODY_W 16
#define BODY_H 28
#define BODY_OX 20
#define BODY_OY 24

/* Tile indices into tileset (21 columns) */
#define TILESET_COLS 21
#define TILE_TL  0
#define TILE_TC  1
#define TILE_TC2 2
#define TILE_TR  3
#define TILE_ML  21
#define TILE_MC  22
#define TILE_MR  24

typedef struct {
	float x, y;        /* sprite top-left in world coords */
	float vx, vy;
	int flip_x;
	int on_ground;
	int jumps;         /* jumps used since last grounded (0 = can jump) */
	lud_anim_t anim;
} player_t;

static int tilemap[MAP_H][MAP_W]; /* -1 = empty, else tile index */
static player_t player;
static float cam_x;

static lud_texture_t tex_tileset;
static lud_texture_t tex_char;
static lud_texture_t tex_bg[3];

static lud_action_t act_left, act_right, act_jump, act_quit;

/* ---- Tile collision ---- */

static int
tile_solid(int tx, int ty)
{
	if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H)
		return 0;
	return tilemap[ty][tx] >= 0;
}

static void
resolve_x(player_t *p)
{
	float bl = p->x + BODY_OX;
	float bt = p->y + BODY_OY;
	int ty0, ty1, tx, ty;

	if (p->vx > 0) {
		tx = (int)((bl + BODY_W) / TILE_SIZE);
		ty0 = (int)(bt / TILE_SIZE);
		ty1 = (int)((bt + BODY_H - 1) / TILE_SIZE);
		for (ty = ty0; ty <= ty1; ty++)
			if (tile_solid(tx, ty)) {
				p->x = tx * TILE_SIZE - BODY_OX - BODY_W;
				p->vx = 0;
				break;
			}
	} else if (p->vx < 0) {
		tx = (int)((bl - 1) / TILE_SIZE);
		ty0 = (int)(bt / TILE_SIZE);
		ty1 = (int)((bt + BODY_H - 1) / TILE_SIZE);
		for (ty = ty0; ty <= ty1; ty++)
			if (tile_solid(tx, ty)) {
				p->x = (tx + 1) * TILE_SIZE - BODY_OX;
				p->vx = 0;
				break;
			}
	}
}

static void
resolve_y(player_t *p)
{
	float bl = p->x + BODY_OX;
	float bt = p->y + BODY_OY;
	int tx0, tx1, ty, tx;

	p->on_ground = 0;
	if (p->vy > 0) { /* falling */
		ty = (int)((bt + BODY_H) / TILE_SIZE);
		tx0 = (int)(bl / TILE_SIZE);
		tx1 = (int)((bl + BODY_W - 1) / TILE_SIZE);
		for (tx = tx0; tx <= tx1; tx++)
			if (tile_solid(tx, ty)) {
				p->y = ty * TILE_SIZE - BODY_OY - BODY_H;
				p->vy = 0;
				p->on_ground = 1;
				break;
			}
	} else if (p->vy < 0) { /* rising */
		ty = (int)((bt - 1) / TILE_SIZE);
		tx0 = (int)(bl / TILE_SIZE);
		tx1 = (int)((bl + BODY_W - 1) / TILE_SIZE);
		for (tx = tx0; tx <= tx1; tx++)
			if (tile_solid(tx, ty)) {
				p->y = (ty + 1) * TILE_SIZE - BODY_OY;
				p->vy = 0;
				break;
			}
	}
}

/* ---- Drawing helpers ---- */

static void
draw_tile(int idx, float wx, float wy)
{
	int col = idx % TILESET_COLS;
	int row = idx / TILESET_COLS;
	lud_sprite_draw(tex_tileset,
		wx, wy, TILE_SIZE, TILE_SIZE,
		col * TILE_SIZE, row * TILE_SIZE, TILE_SIZE, TILE_SIZE);
}

static void
draw_visible_tiles(void)
{
	int x0 = (int)(cam_x / TILE_SIZE);
	int x1, ty, tx;

	if (x0 < 0) x0 = 0;
	x1 = x0 + VIRTUAL_W / TILE_SIZE + 2;
	if (x1 > MAP_W) x1 = MAP_W;
	for (ty = 0; ty < MAP_H; ty++)
		for (tx = x0; tx < x1; tx++)
			if (tilemap[ty][tx] >= 0)
				draw_tile(tilemap[ty][tx],
					tx * TILE_SIZE, ty * TILE_SIZE);
}

static void
draw_player(void)
{
	int f = lud_anim_frame(&player.anim);
	int col = f % CHAR_COLS;
	int row = f / CHAR_COLS;
	lud_sprite_draw_flip(tex_char,
		player.x, player.y, CHAR_FRAME_W, CHAR_FRAME_H,
		col * CHAR_FRAME_W, row * CHAR_FRAME_H,
		CHAR_FRAME_W, CHAR_FRAME_H,
		player.flip_x);
}

/* ---- Tilemap generation ---- */

static void
generate_tilemap(void)
{
	int x, y;

	/* clear */
	for (y = 0; y < MAP_H; y++)
		for (x = 0; x < MAP_W; x++)
			tilemap[y][x] = -1;

	/* bottom row: dirt fill */
	for (x = 0; x < MAP_W; x++)
		tilemap[MAP_H - 1][x] = TILE_MC;

	/* ground surface (row 6): grass with gaps */
	for (x = 0; x < MAP_W; x++) {
		/* leave gaps for pits */
		if ((x >= 12 && x <= 14) || (x >= 35 && x <= 37) ||
		    (x >= 58 && x <= 60))
			continue;
		tilemap[MAP_H - 2][x] = (x % 2 == 0) ? TILE_TC : TILE_TC2;
	}

	/* elevated platform 1: x=18..25, row 4 */
	tilemap[4][18] = TILE_TL;
	for (x = 19; x < 25; x++)
		tilemap[4][x] = TILE_TC;
	tilemap[4][25] = TILE_TR;
	/* fill underneath */
	for (x = 18; x <= 25; x++) {
		tilemap[5][x] = TILE_MC;
	}

	/* elevated platform 2: x=42..48, row 3 */
	tilemap[3][42] = TILE_TL;
	for (x = 43; x < 48; x++)
		tilemap[3][x] = TILE_TC;
	tilemap[3][48] = TILE_TR;
	for (x = 42; x <= 48; x++) {
		tilemap[4][x] = TILE_MC;
		tilemap[5][x] = TILE_MC;
	}

	/* small stepping stone: x=65..68, row 5 */
	tilemap[5][65] = TILE_TL;
	tilemap[5][66] = TILE_TC;
	tilemap[5][67] = TILE_TC2;
	tilemap[5][68] = TILE_TR;
}

/* ---- Callbacks ---- */

static void
init(void)
{
	lud_draw_progress(0, 6, "Loading textures...");
	tex_bg[0] = lud_load_texture("samples/demo04_sprites/assets/background_layer_1.png",
		LUD_FILTER_LINEAR, LUD_FILTER_LINEAR);
	lud_draw_progress(1, 6, NULL);
	tex_bg[1] = lud_load_texture("samples/demo04_sprites/assets/background_layer_2.png",
		LUD_FILTER_LINEAR, LUD_FILTER_LINEAR);
	lud_draw_progress(2, 6, NULL);
	tex_bg[2] = lud_load_texture("samples/demo04_sprites/assets/background_layer_3.png",
		LUD_FILTER_LINEAR, LUD_FILTER_LINEAR);
	lud_draw_progress(3, 6, NULL);
	tex_tileset = lud_load_texture("samples/demo04_sprites/assets/oak_woods_tileset.png",
		LUD_FILTER_NEAREST, LUD_FILTER_NEAREST);
	lud_draw_progress(4, 6, NULL);
	tex_char = lud_load_texture("samples/demo04_sprites/assets/char_blue.png",
		LUD_FILTER_NEAREST, LUD_FILTER_NEAREST);
	lud_draw_progress(5, 6, NULL);

	generate_tilemap();

	/* spawn player above ground */
	player.x = 2 * TILE_SIZE;
	player.y = (MAP_H - 2) * TILE_SIZE - CHAR_FRAME_H;
	player.vx = 0;
	player.vy = 0;
	player.flip_x = 0;
	player.on_ground = 0;
	lud_anim_init(&player.anim, 0, 5, 8.0f, 1); /* idle */

	cam_x = 0;

	/* action bindings */
	act_left = lud_make_action("left");
	lud_bind_key(LUD_KEY_LEFT, act_left);
	lud_bind_key(LUD_KEY_A, act_left);

	act_right = lud_make_action("right");
	lud_bind_key(LUD_KEY_RIGHT, act_right);
	lud_bind_key(LUD_KEY_D, act_right);

	act_jump = lud_make_action("jump");
	lud_bind_key(LUD_KEY_SPACE, act_jump);
	lud_bind_key(LUD_KEY_UP, act_jump);
	lud_bind_key(LUD_KEY_W, act_jump);

	act_quit = lud_make_action("quit");
	lud_bind_key(LUD_KEY_ESCAPE, act_quit);

	lud_draw_progress(6, 6, "Ready");
}

static void
frame(float dt)
{
	float target, max_cam;
	float bg_rates[3] = { 0.0f, 0.3f, 0.6f };
	int i;

	/* --- Input --- */
	if (lud_action_pressed(act_quit))
		lud_quit();

	player.vx = 0;
	if (lud_action_down(act_left)) {
		player.vx = -MOVE_SPEED;
		player.flip_x = 1;
	}
	if (lud_action_down(act_right)) {
		player.vx = MOVE_SPEED;
		player.flip_x = 0;
	}
	if (lud_action_pressed(act_jump) && player.jumps < 2) {
		player.vy = JUMP_VEL;
		player.on_ground = 0;
		player.jumps++;
	}

	/* --- Physics --- */
	player.vy += GRAVITY * dt;
	player.x += player.vx * dt;
	resolve_x(&player);
	player.y += player.vy * dt;
	resolve_y(&player);

	if (player.on_ground)
		player.jumps = 0;

	/* --- Clamp to world bounds --- */
	if (player.x < -BODY_OX)
		player.x = -BODY_OX;
	if (player.x + BODY_OX + BODY_W > MAP_W * TILE_SIZE)
		player.x = MAP_W * TILE_SIZE - BODY_OX - BODY_W;

	/* --- Animation state --- */
	if (player.on_ground) {
		if (player.vx != 0)
			lud_anim_play(&player.anim, 16, 23, 10.0f, 1); /* run */
		else
			lud_anim_play(&player.anim, 0, 5, 8.0f, 1); /* idle */
	} else {
		if (player.vy < 0)
			lud_anim_play(&player.anim, 24, 31, 10.0f, 0); /* jump */
		else
			lud_anim_play(&player.anim, 32, 39, 10.0f, 1); /* fall */
	}
	lud_anim_update(&player.anim, dt);

	/* --- Camera --- */
	target = player.x + CHAR_FRAME_W / 2.0f - VIRTUAL_W / 2.0f;
	cam_x += (target - cam_x) * 5.0f * dt;
	if (cam_x < 0) cam_x = 0;
	max_cam = MAP_W * TILE_SIZE - VIRTUAL_W;
	if (cam_x > max_cam) cam_x = max_cam;

	/* --- Render --- */
	lud_viewport(0, 0, lud_width(), lud_height());
	lud_clear(0.4f, 0.6f, 0.9f, 1.0f);

	/* Parallax backgrounds */
	lud_sprite_begin(0, 0, VIRTUAL_W, VIRTUAL_H);
	for (i = 0; i < 3; i++) {
		float sx = cam_x * bg_rates[i];
		float off = sx - (int)(sx / VIRTUAL_W) * VIRTUAL_W;
		if (off < 0) off += VIRTUAL_W;
		/* first copy */
		lud_sprite_draw(tex_bg[i],
			-off, 0, VIRTUAL_W, VIRTUAL_H,
			0, 0, VIRTUAL_W, VIRTUAL_H);
		/* second copy to fill the gap */
		lud_sprite_draw(tex_bg[i],
			VIRTUAL_W - off, 0, VIRTUAL_W, VIRTUAL_H,
			0, 0, VIRTUAL_W, VIRTUAL_H);
	}
	lud_sprite_end();

	/* World-space sprites — tilemap + player */
	lud_sprite_begin(cam_x, 0, VIRTUAL_W, VIRTUAL_H);
	draw_visible_tiles();
	draw_player();
	lud_sprite_end();
}

static void
cleanup(void)
{
	int i;
	lud_destroy_texture(tex_tileset);
	lud_destroy_texture(tex_char);
	for (i = 0; i < 3; i++)
		lud_destroy_texture(tex_bg[i]);
}

int
main(int argc, const char *argv[])
{
	return lud_run(&(lud_desc_t){
		.app_name = "demo04_sprites",
		.width = 960,
		.height = 540,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.argc = argc,
		.argv = argv,
	});
}
