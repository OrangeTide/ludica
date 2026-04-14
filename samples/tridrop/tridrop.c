/*
 * tridrop — Triangle-based falling block puzzle.
 *
 * Inspired by xTetris (1989), a DOS-era puzzle game with triangular
 * pieces instead of the traditional rectangular tetrominoes.
 *
 * Two game modes:
 *   NEO     — Single-cell pieces with triangle sliding physics.
 *             Triangles slide off complementary ramps; blocked slides
 *             merge into solid blocks. X destructor power-ups.
 *   TRIDROP — Paired pieces with all four triangle orientations
 *             (UL/UR/BL/BR) for more complex piece shapes.
 *
 * Left/Right: move    Up: rotate    Down: soft drop
 * Space: hard drop    P: pause      Escape: pause / quit
 */

#include "ludica.h"
#include "ludica_gfx.h"
#include "ludica_font.h"
#include <string.h>

#define VW    320
#define VH    240
#define COLS  10
#define ROWS  20
#define CELL  10
#define FX    16		/* field top-left X */
#define FY    20		/* field top-left Y */

/* --- Cell types --------------------------------------------------------- */

enum {
	EMPTY = 0,
	TRI_UL,		/* upper-left triangle  (fills above \ diagonal) */
	TRI_UR,		/* upper-right triangle (fills above / diagonal) */
	TRI_BL,		/* bottom-left triangle (fills below / diagonal) */
	TRI_BR,		/* bottom-right triangle (fills below \ diagonal) */
	FULL		/* solid block */
};

/* Clockwise rotation: UL -> UR -> BR -> BL -> UL (tridrop mode) */
static int
orient_cw(int o)
{
	static const int tbl[] = { 0, TRI_UR, TRI_BR, TRI_UL, TRI_BL, FULL };
	return tbl[o];
}

static int
is_complement(int a, int b)
{
	return (a == TRI_UL && b == TRI_BR) || (a == TRI_BR && b == TRI_UL) ||
	       (a == TRI_UR && b == TRI_BL) || (a == TRI_BL && b == TRI_UR);
}

/* Side solidity patterns: bits = top(3), right(2), bottom(1), left(0).
 * Each triangle has two solid sides (adjacent to its corner) and two
 * empty sides (at the hypotenuse / "face" corner). */
#define SIDE_TOP    8
#define SIDE_RIGHT  4
#define SIDE_BOTTOM 2
#define SIDE_LEFT   1

static int
side_pattern(int orient)
{
	switch (orient) {
	case TRI_UL: return SIDE_TOP | SIDE_LEFT;		/* 1001 */
	case TRI_UR: return SIDE_TOP | SIDE_RIGHT;		/* 1100 */
	case TRI_BL: return SIDE_BOTTOM | SIDE_LEFT;		/* 0011 */
	case TRI_BR: return SIDE_BOTTOM | SIDE_RIGHT;		/* 0110 */
	case FULL:   return SIDE_TOP|SIDE_RIGHT|SIDE_BOTTOM|SIDE_LEFT;
	default:     return 0;
	}
}

/* --- Colors ------------------------------------------------------------- */

static const float palette[][3] = {
	{ 0.00f, 0.80f, 0.80f },	/* 0 cyan    */
	{ 0.80f, 0.80f, 0.00f },	/* 1 yellow  */
	{ 0.20f, 0.80f, 0.20f },	/* 2 green   */
	{ 0.90f, 0.25f, 0.25f },	/* 3 red     */
	{ 0.35f, 0.35f, 0.95f },	/* 4 blue    */
	{ 0.85f, 0.20f, 0.85f },	/* 5 magenta */
	{ 0.90f, 0.55f, 0.10f },	/* 6 orange  */
};
#define NCOLORS 7

/* --- Game modes --------------------------------------------------------- */

enum { MODE_NEO, MODE_TRIDROP, NUM_MODES };

static const char *mode_names[] = { "NEO", "TRIDROP" };
static const char *mode_desc[]  = {
	"Single pieces + sliding",
	"Paired pieces",
};

/* --- Pieces ------------------------------------------------------------- */

struct tri  { int dx, dy, orient; };
struct piece { struct tri t[2]; int color; int is_x; int count; };

/* Tridrop pieces: paired, all four orientations */
#define NPIECES_TRIDROP 7
static const struct tri tmpl_tridrop[NPIECES_TRIDROP][2] = {
	{{ 0, 0, TRI_UL }, { 0, 0, TRI_BR }},	/* 0  square  */
	{{ 0, 0, TRI_UL }, { 1, 0, TRI_UL }},	/* 1  wedge   */
	{{ 0, 0, TRI_UL }, { 1, 0, TRI_UR }},	/* 2  peak    */
	{{ 0, 0, TRI_BL }, { 1, 0, TRI_BR }},	/* 3  valley  */
	{{ 0, 0, TRI_UL }, { 0, 1, TRI_UL }},	/* 4  pillar  */
	{{ 0, 0, TRI_BR }, { 1, 0, TRI_UL }},	/* 5  zigzag  */
	{{ 0, 0, TRI_BL }, { 1, 1, TRI_UR }},	/* 6  step    */
};

/* --- RNG (xorshift32) --------------------------------------------------- */

static unsigned rng_s = 1;

static int
rng(int n)
{
	rng_s ^= rng_s << 13;
	rng_s ^= rng_s >> 17;
	rng_s ^= rng_s << 5;
	return (int)(rng_s % (unsigned)n);
}

/* --- Game state --------------------------------------------------------- */

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER };

static int mode;
static int title_sel;		/* mode selection on title screen */
static int pause_sel;		/* pause menu selection */
static int board_fill[ROWS][COLS];
static int board_color[ROWS][COLS];
static struct piece cur, nxt;
static int px, py;		/* current piece grid position */
static int gstate;
static int score, level, lines_total;
static int pieces_dropped;	/* counter for X piece scheduling */
static float drop_timer;
static int lock_wait;		/* one-tick grace period before locking */
static float elapsed;
static lud_font_t font;

/* --- Piece operations --------------------------------------------------- */

static struct piece
random_piece(void)
{
	static const int neo_orients[] = {
		TRI_UL, TRI_UR, TRI_BL, TRI_BR
	};
	struct piece p;
	int t, roll;

	p.is_x = 0;

	if (mode == MODE_NEO) {
		/*
		 * NEO distribution (roll 0-9):
		 *   0-5: two-triangle pair (from tridrop templates)
		 *   6-7: single FULL square
		 *     8: X destructor (or triangle if too early)
		 *     9: single triangle
		 */
		roll = rng(10);
		if (roll <= 5) {
			/* two-triangle pair */
			t = rng(NPIECES_TRIDROP);
			p.t[0] = tmpl_tridrop[t][0];
			p.t[1] = tmpl_tridrop[t][1];
			p.color = rng(NCOLORS);
			p.count = 2;
			return p;
		}
		if (roll <= 7) {
			/* single FULL square */
			p.t[0] = (struct tri){ 0, 0, FULL };
			p.color = rng(NCOLORS);
			p.count = 1;
			return p;
		}
		if (roll == 8 && pieces_dropped > 0) {
			/* X destructor */
			p.t[0] = (struct tri){ 0, 0, FULL };
			p.color = 3;	/* red */
			p.is_x = 1;
			p.count = 1;
			return p;
		}
		/* single triangle */
		t = rng(4);
		p.t[0] = (struct tri){ 0, 0, neo_orients[t] };
		p.color = rng(NCOLORS);
		p.count = 1;
		return p;
	}

	/* TRIDROP: paired pieces */
	t = rng(NPIECES_TRIDROP);
	p.t[0] = tmpl_tridrop[t][0];
	p.t[1] = tmpl_tridrop[t][1];
	p.color = rng(NCOLORS);
	p.count = 2;
	return p;
}

static void
rotate_cw_piece(struct piece *p)
{
	int i;

	for (i = 0; i < p->count; i++) {
		int ndx = -p->t[i].dy;
		int ndy =  p->t[i].dx;
		p->t[i].dx = ndx;
		p->t[i].dy = ndy;
		p->t[i].orient = orient_cw(p->t[i].orient);
	}
	/* normalize to non-negative offsets (paired pieces only) */
	if (p->count == 2) {
		int mdx = p->t[0].dx < p->t[1].dx ? p->t[0].dx : p->t[1].dx;
		int mdy = p->t[0].dy < p->t[1].dy ? p->t[0].dy : p->t[1].dy;
		for (i = 0; i < 2; i++) {
			p->t[i].dx -= mdx;
			p->t[i].dy -= mdy;
		}
	}
}

static int
fits(const struct piece *p, int cx, int cy)
{
	int i;

	for (i = 0; i < p->count; i++) {
		int c = cx + p->t[i].dx;
		int r = cy + p->t[i].dy;
		int f;

		if (c < 0 || c >= COLS || r < 0 || r >= ROWS)
			return 0;
		if (p->is_x)
			continue;
		f = board_fill[r][c];
		if (f == FULL)
			return 0;
		if (f != EMPTY && !is_complement(f, p->t[i].orient))
			return 0;
	}
	/* same-cell pair must be complementary (unless X piece) */
	if (!p->is_x && p->count == 2 &&
	    p->t[0].dx == p->t[1].dx && p->t[0].dy == p->t[1].dy)
		if (!is_complement(p->t[0].orient, p->t[1].orient))
			return 0;
	return 1;
}

/* X destructor: check if piece can continue falling.
 * X stops when cells below are occupied or at bottom. */
static int
fits_x(const struct piece *p, int cx, int cy)
{
	int i;

	for (i = 0; i < p->count; i++) {
		int c = cx + p->t[i].dx;
		int r = cy + p->t[i].dy;

		if (c < 0 || c >= COLS || r >= ROWS)
			return 0;
		if (r < 0)
			continue;
		if (board_fill[r][c] != EMPTY)
			return 0;
	}
	return 1;
}

/* NEO horizontal move check.
 * Returns: 0 = blocked, 1 = can move (all empty), 2 = move + merge. */
static int
neo_check_horizontal(int dc)
{
	int i, any_merge = 0;
	int side_move = (dc > 0) ? SIDE_RIGHT : SIDE_LEFT;
	int side_face = (dc > 0) ? SIDE_LEFT : SIDE_RIGHT;

	for (i = 0; i < cur.count; i++) {
		int c = px + dc + cur.t[i].dx;
		int r = py + cur.t[i].dy;
		int dest, move_sp, dest_sp;

		if (c < 0 || c >= COLS || r < 0 || r >= ROWS)
			return 0;
		dest = board_fill[r][c];
		if (dest == EMPTY)
			continue;
		if (dest == FULL)
			return 0;

		move_sp = side_pattern(cur.t[i].orient);
		dest_sp = side_pattern(dest);

		/* both sides facing each other must be open */
		if ((move_sp & side_move) || (dest_sp & side_face))
			return 0;
		/* must be complementary to merge */
		if ((move_sp ^ dest_sp) != 0x0f)
			return 0;
		any_merge = 1;
	}
	return any_merge ? 2 : 1;
}

static void
lock(void)
{
	int i;

	if (cur.is_x) {
		/* X destructor: clear a cross pattern around landing */
		static const int dx[] = { 0, -1, 1, 0, 0 };
		static const int dy[] = { 0, 0, 0, -1, 1 };
		int j;

		for (i = 0; i < cur.count; i++) {
			int bc = px + cur.t[i].dx;
			int br = py + cur.t[i].dy;

			for (j = 0; j < 5; j++) {
				int cc = bc + dx[j];
				int rr = br + dy[j];

				if (cc >= 0 && cc < COLS &&
				    rr >= 0 && rr < ROWS) {
					board_fill[rr][cc] = EMPTY;
					board_color[rr][cc] = 0;
				}
			}
		}
		score += 50;
		return;
	}

	for (i = 0; i < cur.count; i++) {
		int c = px + cur.t[i].dx;
		int r = py + cur.t[i].dy;

		if (c < 0 || c >= COLS || r < 0 || r >= ROWS)
			continue;
		if (board_fill[r][c] == EMPTY) {
			board_fill[r][c] = cur.t[i].orient;
			board_color[r][c] = cur.color;
		} else if (is_complement(board_fill[r][c], cur.t[i].orient)) {
			board_fill[r][c] = FULL;
		}
	}
}

static int
clear_rows(void)
{
	int cleared = 0, r, c;

	for (r = ROWS - 1; r >= 0; r--) {
		int ok = 1;

		for (c = 0; c < COLS; c++) {
			if (board_fill[r][c] != FULL) {
				ok = 0;
				break;
			}
		}
		if (!ok)
			continue;
		/* shift everything above down by one row */
		{
			int rr;
			for (rr = r; rr > 0; rr--) {
				memcpy(board_fill[rr], board_fill[rr - 1],
				       sizeof board_fill[0]);
				memcpy(board_color[rr], board_color[rr - 1],
				       sizeof board_color[0]);
			}
		}
		memset(board_fill[0], 0, sizeof board_fill[0]);
		memset(board_color[0], 0, sizeof board_color[0]);
		cleared++;
		r++;	/* re-check this row (new content shifted in) */
	}
	return cleared;
}

/* NEO mode: step piece down one row with sliding physics.
 * Handles multi-cell pieces: the whole group slides together.
 * If cells disagree on slide direction, the group merges instead.
 * Returns: 0 = piece still falling, 1 = piece locked/merged. */
static int
neo_step_down(void)
{
	int i, all_empty, slide_dir, slide_set;

	/* Phase 1: check each cell's "below" position */
	all_empty = 1;
	slide_dir = 0;
	slide_set = 0;	/* have we determined a slide direction? */

	for (i = 0; i < cur.count; i++) {
		int c = px + cur.t[i].dx;
		int br = py + cur.t[i].dy + 1;
		int fall_sp, land_sp, below;

		/* floor = hard stop */
		if (br >= ROWS) {
			lock();
			return 1;
		}

		below = board_fill[br][c];
		if (below == EMPTY)
			continue;

		all_empty = 0;
		fall_sp = side_pattern(cur.t[i].orient);
		land_sp = side_pattern(below);

		/* solid bottom or solid top = hard stop */
		if ((fall_sp & SIDE_BOTTOM) || (land_sp & SIDE_TOP)) {
			lock();
			return 1;
		}

		/* must be complementary to interact */
		if ((fall_sp ^ land_sp) != 0x0f) {
			lock();
			return 1;
		}

		/* determine slide direction from landing piece */
		{
			int d = 0;

			if (!(land_sp & SIDE_LEFT))
				d = -1;
			else if (!(land_sp & SIDE_RIGHT))
				d = 1;

			if (!slide_set) {
				slide_dir = d;
				slide_set = 1;
			} else if (d != slide_dir) {
				/* cells disagree on direction -> merge */
				goto merge;
			}
		}
	}

	/* all cells below are empty -> fall normally */
	if (all_empty) {
		py++;
		return 0;
	}

	/* try to slide the whole group */
	if (slide_dir != 0) {
		int ok = 1;

		for (i = 0; i < cur.count; i++) {
			int nc = px + slide_dir + cur.t[i].dx;
			int nr = py + 1 + cur.t[i].dy;

			if (nc < 0 || nc >= COLS || nr >= ROWS ||
			    board_fill[nr][nc] != EMPTY) {
				ok = 0;
				break;
			}
		}
		if (ok) {
			py++;
			px += slide_dir;
			return 0;
		}
	}

merge:
	/* merge all complementary pairs, place rest normally */
	py++;
	lock();
	return 1;
}

/* Read-only version of neo_step_down: returns 1 if the piece would
 * lock or merge on the next step, 0 if it would fall or slide. */
static int
neo_check_grounded(void)
{
	int i, all_empty, slide_dir, slide_set;

	all_empty = 1;
	slide_dir = 0;
	slide_set = 0;

	for (i = 0; i < cur.count; i++) {
		int c = px + cur.t[i].dx;
		int br = py + cur.t[i].dy + 1;
		int fall_sp, land_sp, below;

		if (br >= ROWS)
			return 1;
		below = board_fill[br][c];
		if (below == EMPTY)
			continue;
		all_empty = 0;
		fall_sp = side_pattern(cur.t[i].orient);
		land_sp = side_pattern(below);
		if ((fall_sp & SIDE_BOTTOM) || (land_sp & SIDE_TOP))
			return 1;
		if ((fall_sp ^ land_sp) != 0x0f)
			return 1;
		{
			int d = 0;

			if (!(land_sp & SIDE_LEFT))
				d = -1;
			else if (!(land_sp & SIDE_RIGHT))
				d = 1;
			if (!slide_set) {
				slide_dir = d;
				slide_set = 1;
			} else if (d != slide_dir) {
				return 1;
			}
		}
	}

	if (all_empty)
		return 0;

	/* check if slide would succeed */
	if (slide_dir != 0) {
		for (i = 0; i < cur.count; i++) {
			int nc = px + slide_dir + cur.t[i].dx;
			int nr = py + 1 + cur.t[i].dy;

			if (nc < 0 || nc >= COLS || nr >= ROWS ||
			    board_fill[nr][nc] != EMPTY)
				return 1;
		}
		return 0;
	}
	return 1;
}

static float
drop_speed(void)
{
	int l = level - 1;

	if (l > 15)
		l = 15;
	return 0.8f - (float)l * 0.04f;
}

static void
spawn(void)
{
	cur = nxt;
	nxt = random_piece();
	px = COLS / 2 - 1;
	py = 0;
	pieces_dropped++;
	lock_wait = 0;
	if (!fits(&cur, px, py))
		gstate = ST_OVER;
}

static void
reset_game(void)
{
	memset(board_fill, 0, sizeof board_fill);
	memset(board_color, 0, sizeof board_color);
	score = 0;
	level = 1;
	lines_total = 0;
	pieces_dropped = 0;
	drop_timer = 0;
	nxt = random_piece();
	spawn();
	gstate = ST_PLAY;
}

/* --- Drawing ------------------------------------------------------------ */

static void
draw_triangle(float cx, float cy, int orient,
              float r, float g, float b, float a)
{
	int y;
	float s = (float)CELL;

	for (y = 0; y < CELL; y++) {
		float fy = (float)y;
		float x0 = 0, x1 = 0;

		switch (orient) {
		case TRI_UL: x0 = 0;              x1 = s - fy;       break;
		case TRI_UR: x0 = fy;             x1 = s;            break;
		case TRI_BL: x0 = 0;              x1 = fy + 1.0f;    break;
		case TRI_BR: x0 = s - fy - 1.0f;  x1 = s;            break;
		default: return;
		}
		if (x1 > x0)
			lud_sprite_rect(cx + x0, cy + fy, x1 - x0, 1,
			                r, g, b, a);
	}
}

static void
draw_x_mark(float cx, float cy, float r, float g, float b, float a)
{
	int i;

	for (i = 0; i < CELL; i++) {
		float fi = (float)i;

		/* \ diagonal, 2px wide */
		lud_sprite_rect(cx + fi, cy + fi, 2, 1, r, g, b, a);
		/* / diagonal, 2px wide */
		if (CELL - 2 - i >= 0)
			lud_sprite_rect(cx + (float)(CELL - 2 - i), cy + fi,
			                2, 1, r, g, b, a);
	}
}

static void
draw_cell(int c, int r)
{
	float cx = FX + (float)(c * CELL);
	float cy = FY + (float)(r * CELL);
	int f = board_fill[r][c];
	int ci = board_color[r][c];

	if (f == EMPTY)
		return;
	if (f == FULL)
		lud_sprite_rect(cx, cy, CELL, CELL,
		                palette[ci][0], palette[ci][1],
		                palette[ci][2], 1.0f);
	else
		draw_triangle(cx, cy, f,
		              palette[ci][0], palette[ci][1],
		              palette[ci][2], 1.0f);
}

static void
draw_piece(const struct piece *p, int cx, int cy,
           float ox, float oy, float alpha)
{
	int i;

	if (p->is_x) {
		/* X destructor: draw pulsing X symbol */
		float x = ox + (float)((cx + p->t[0].dx) * CELL);
		float y = oy + (float)((cy + p->t[0].dy) * CELL);
		float pulse = elapsed * 6.0f;

		pulse = pulse - (int)pulse;
		pulse = 0.6f + 0.4f * pulse;
		lud_sprite_rect(x, y, CELL, CELL,
		                0.15f, 0.0f, 0.0f, alpha);
		draw_x_mark(x, y, pulse, pulse * 0.3f, 0.0f, alpha);
		return;
	}

	/* same-cell complementary pair -> draw as full block */
	if (p->count == 2 &&
	    p->t[0].dx == p->t[1].dx && p->t[0].dy == p->t[1].dy &&
	    is_complement(p->t[0].orient, p->t[1].orient)) {
		float x = ox + (float)((cx + p->t[0].dx) * CELL);
		float y = oy + (float)((cy + p->t[0].dy) * CELL);

		lud_sprite_rect(x, y, CELL, CELL,
		                palette[p->color][0], palette[p->color][1],
		                palette[p->color][2], alpha);
		return;
	}
	for (i = 0; i < p->count; i++) {
		float x = ox + (float)((cx + p->t[i].dx) * CELL);
		float y = oy + (float)((cy + p->t[i].dy) * CELL);

		if (p->t[i].orient == FULL)
			lud_sprite_rect(x, y, CELL, CELL,
			                palette[p->color][0], palette[p->color][1],
			                palette[p->color][2], alpha);
		else
			draw_triangle(x, y, p->t[i].orient,
			              palette[p->color][0], palette[p->color][1],
			              palette[p->color][2], alpha);
	}
}

static void
draw_board(void)
{
	int r, c;

	/* background */
	lud_sprite_rect(FX, FY, COLS * CELL, ROWS * CELL,
	                0.04f, 0.04f, 0.07f, 1.0f);

	/* grid lines */
	for (c = 0; c <= COLS; c++)
		lud_sprite_rect(FX + (float)(c * CELL), FY, 1,
		                ROWS * CELL, 0.12f, 0.12f, 0.18f, 0.5f);
	for (r = 0; r <= ROWS; r++)
		lud_sprite_rect(FX, FY + (float)(r * CELL),
		                COLS * CELL, 1, 0.12f, 0.12f, 0.18f, 0.5f);

	/* locked cells */
	for (r = 0; r < ROWS; r++)
		for (c = 0; c < COLS; c++)
			draw_cell(c, r);

	/* border */
	lud_sprite_rect_lines(FX - 1, FY - 1,
	                      COLS * CELL + 2, ROWS * CELL + 2,
	                      0.5f, 0.5f, 0.6f, 1.0f);
}

static void
draw_ghost(void)
{
	int gx = px, gy = py;

	if (cur.is_x) {
		while (fits_x(&cur, gx, gy + 1))
			gy++;
	} else if (mode == MODE_NEO) {
		/* simulate NEO sliding path for the ghost */
		for (;;) {
			int i, all_empty, slide_dir, slide_set, done;

			all_empty = 1;
			slide_dir = 0;
			slide_set = 0;
			done = 0;

			for (i = 0; i < cur.count; i++) {
				int c = gx + cur.t[i].dx;
				int br = gy + cur.t[i].dy + 1;
				int fall_sp, land_sp, below;

				if (br >= ROWS) { done = 1; break; }
				below = board_fill[br][c];
				if (below == EMPTY) continue;
				all_empty = 0;
				fall_sp = side_pattern(cur.t[i].orient);
				land_sp = side_pattern(below);
				if ((fall_sp & SIDE_BOTTOM) ||
				    (land_sp & SIDE_TOP)) {
					done = 1; break;
				}
				if ((fall_sp ^ land_sp) != 0x0f) {
					done = 1; break;
				}
				{
					int d = 0;
					if (!(land_sp & SIDE_LEFT)) d = -1;
					else if (!(land_sp & SIDE_RIGHT)) d = 1;
					if (!slide_set) {
						slide_dir = d;
						slide_set = 1;
					} else if (d != slide_dir) {
						gy++; done = 1; break;
					}
				}
			}
			if (done) break;
			if (all_empty) { gy++; continue; }

			/* try slide */
			if (slide_dir != 0) {
				int ok = 1;
				for (i = 0; i < cur.count; i++) {
					int nc = gx + slide_dir + cur.t[i].dx;
					int nr = gy + 1 + cur.t[i].dy;
					if (nc < 0 || nc >= COLS ||
					    nr >= ROWS ||
					    board_fill[nr][nc] != EMPTY) {
						ok = 0; break;
					}
				}
				if (ok) {
					gy++;
					gx += slide_dir;
					continue;
				}
			}
			/* merge */
			gy++;
			break;
		}
	} else {
		while (fits(&cur, gx, gy + 1))
			gy++;
	}
	if (gy > py || gx != px)
		draw_piece(&cur, gx, gy, FX, FY, 0.2f);
}

/* int-to-string without snprintf */
static void
istr(char *buf, int v)
{
	char tmp[12];
	int i = 0, j = 0;

	if (v == 0) {
		buf[0] = '0';
		buf[1] = 0;
		return;
	}
	while (v > 0) {
		tmp[i++] = '0' + v % 10;
		v /= 10;
	}
	while (i > 0)
		buf[j++] = tmp[--i];
	buf[j] = 0;
}

static void
draw_panel(void)
{
	float sx = FX + COLS * CELL + 16;
	char buf[16];

	lud_draw_text(font, sx, FY, 2, "TRIDROP");
	lud_draw_text(font, sx, FY + 18, 1, mode_names[mode]);

	lud_draw_text(font, sx, FY + 36, 1, "SCORE");
	istr(buf, score);
	lud_draw_text(font, sx, FY + 48, 1, buf);

	lud_draw_text(font, sx, FY + 66, 1, "LEVEL");
	istr(buf, level);
	lud_draw_text(font, sx, FY + 78, 1, buf);

	lud_draw_text(font, sx, FY + 96, 1, "LINES");
	istr(buf, lines_total);
	lud_draw_text(font, sx, FY + 108, 1, buf);

	/* next piece preview */
	lud_draw_text(font, sx, FY + 130, 1, "NEXT");
	{
		float bx = sx;
		float by = FY + 143;

		lud_sprite_rect(bx, by, CELL * 3, CELL * 3,
		                0.04f, 0.04f, 0.07f, 1.0f);
		lud_sprite_rect_lines(bx - 1, by - 1,
		                      CELL * 3 + 2, CELL * 3 + 2,
		                      0.4f, 0.4f, 0.5f, 1.0f);
		draw_piece(&nxt, 0, 0, bx + CELL / 2, by + CELL / 2, 1.0f);
	}

	/* controls hint */
	lud_draw_text(font, FX, VH - 10, 1,
	              "Arrows:move Up:rot Space:drop P:pause");
}

/* --- Title screen ------------------------------------------------------- */

static void
draw_title(void)
{
	int i;
	float blink;

	lud_draw_text_centered(font, VW / 2, 40, 2, "TRIDROP");
	lud_draw_text_centered(font, VW / 2, 65, 1,
	                       "Triangle Block Puzzle");

	/* mode selection */
	for (i = 0; i < NUM_MODES; i++) {
		float y = 100.0f + (float)i * 20.0f;

		if (i == title_sel)
			lud_draw_text(font, 80, y, 1, ">");
		lud_draw_text(font, 95, y, 1, mode_names[i]);
		lud_draw_text(font, 170, y, 1, mode_desc[i]);
	}

	/* blinking prompt */
	blink = elapsed * 4.0f;
	blink = blink - (int)blink;
	if (blink > 0.3f)
		lud_draw_text_centered(font, VW / 2, 160, 1,
		                       "Press ENTER to start");

	lud_draw_text_centered(font, VW / 2, VH - 20, 1,
	                       "Up/Down:mode  Enter:start");
}

/* --- Callbacks ---------------------------------------------------------- */

static void
init(void)
{
	font = lud_make_default_font();
	rng_s = (unsigned)(lud_time() * 1000.0f) | 1u;
	gstate = ST_TITLE;
	title_sel = MODE_NEO;
}

static int
can_drop(void)
{
	if (cur.is_x)
		return fits_x(&cur, px, py + 1);
	return fits(&cur, px, py + 1);
}

static void
do_clear_and_spawn(void)
{
	int n;

	n = clear_rows();
	if (n > 0) {
		static const int pts[] = { 0, 100, 300, 500, 800 };

		lines_total += n;
		score += pts[n < 5 ? n : 4] * level;
		level = lines_total / 10 + 1;
	}
	spawn();
	drop_timer = 0;
}

static void
do_lock_and_clear(void)
{
	lock();
	do_clear_and_spawn();
}

static int
on_event(const lud_event_t *ev)
{
	if (ev->type != LUD_EV_KEY_DOWN)
		return 0;

	/* Escape: enter pause menu, or quit from title/over */
	if (ev->key.keycode == LUD_KEY_ESCAPE) {
		if (gstate == ST_PLAY) {
			pause_sel = 0;
			gstate = ST_PAUSE;
			return 1;
		}
		if (gstate == ST_PAUSE) {
			gstate = ST_PLAY;
			return 1;
		}
		lud_quit();
		return 1;
	}

	switch (gstate) {
	case ST_TITLE:
		if (ev->key.keycode == LUD_KEY_UP) {
			title_sel--;
			if (title_sel < 0)
				title_sel = NUM_MODES - 1;
		} else if (ev->key.keycode == LUD_KEY_DOWN) {
			title_sel++;
			if (title_sel >= NUM_MODES)
				title_sel = 0;
		} else if (ev->key.keycode == LUD_KEY_ENTER ||
		           ev->key.keycode == LUD_KEY_SPACE) {
			mode = title_sel;
			reset_game();
		}
		return 1;

	case ST_OVER:
		if (ev->key.keycode == LUD_KEY_ENTER ||
		    ev->key.keycode == LUD_KEY_SPACE) {
			gstate = ST_TITLE;
		}
		return 1;

	case ST_PAUSE:
		if (ev->key.keycode == LUD_KEY_UP) {
			pause_sel--;
			if (pause_sel < 0) pause_sel = 2;
		} else if (ev->key.keycode == LUD_KEY_DOWN) {
			pause_sel++;
			if (pause_sel > 2) pause_sel = 0;
		} else if (ev->key.keycode == LUD_KEY_ENTER ||
		           ev->key.keycode == LUD_KEY_SPACE) {
			if (pause_sel == 0)
				gstate = ST_PLAY;	/* continue */
			else if (pause_sel == 1)
				gstate = ST_TITLE;	/* quit to title */
			else
				lud_quit();		/* exit */
		} else if (ev->key.keycode == LUD_KEY_P) {
			gstate = ST_PLAY;
		}
		return 1;

	case ST_PLAY:
		if (ev->key.keycode == LUD_KEY_P) {
			pause_sel = 0;
			gstate = ST_PAUSE;
			return 1;
		}
		if (ev->key.keycode == LUD_KEY_LEFT) {
			if (mode == MODE_NEO && !cur.is_x) {
				int r = neo_check_horizontal(-1);

				if (r >= 1)
					px--;
			} else {
				if (fits(&cur, px - 1, py))
					px--;
			}
		} else if (ev->key.keycode == LUD_KEY_RIGHT) {
			if (mode == MODE_NEO && !cur.is_x) {
				int r = neo_check_horizontal(1);

				if (r >= 1)
					px++;
			} else {
				if (fits(&cur, px + 1, py))
					px++;
			}
		} else if (ev->key.keycode == LUD_KEY_UP && !ev->key.repeat) {
			if (!cur.is_x) {
				struct piece rot = cur;

				rotate_cw_piece(&rot);
				if (fits(&rot, px, py))
					cur = rot;
				else if (fits(&rot, px - 1, py)) {
					cur = rot;
					px--;
				} else if (fits(&rot, px + 1, py)) {
					cur = rot;
					px++;
				}
			}
		} else if (ev->key.keycode == LUD_KEY_DOWN) {
			if (mode == MODE_NEO && !cur.is_x) {
				if (!neo_step_down()) {
					score++;
					drop_timer = 0;
				} else {
					do_clear_and_spawn();
				}
			} else {
				if (can_drop()) {
					py++;
					score++;
					drop_timer = 0;
				}
			}
		} else if (ev->key.keycode == LUD_KEY_SPACE && !ev->key.repeat) {
			if (mode == MODE_NEO && !cur.is_x) {
				while (!neo_step_down())
					score += 2;
				do_clear_and_spawn();
			} else {
				while (can_drop()) {
					py++;
					score += 2;
				}
				do_lock_and_clear();
			}
		}
		return 1;
	}

	return 0;
}

static void
frame(float dt)
{
	elapsed += dt;

	/* gravity */
	if (gstate == ST_PLAY) {
		int grounded;

		/* determine if piece is grounded (would lock on next step) */
		if (mode == MODE_NEO && !cur.is_x)
			grounded = neo_check_grounded();
		else
			grounded = !can_drop();

		if (grounded) {
			/* one-tick grace period before locking */
			drop_timer += dt;
			if (drop_timer >= drop_speed()) {
				drop_timer -= drop_speed();
				if (!lock_wait) {
					lock_wait = 1;
				} else {
					lock_wait = 0;
					if (mode == MODE_NEO && !cur.is_x) {
						neo_step_down();
						do_clear_and_spawn();
					} else {
						do_lock_and_clear();
					}
				}
			}
		} else {
			lock_wait = 0;
			drop_timer += dt;
			if (drop_timer >= drop_speed()) {
				drop_timer -= drop_speed();
				if (mode == MODE_NEO && !cur.is_x) {
					if (neo_step_down())
						do_clear_and_spawn();
				} else {
					py++;
				}
			}
		}
	}

	lud_viewport(0, 0, lud_width(), lud_height());
	lud_clear(0.06f, 0.06f, 0.10f, 1.0f);
	lud_sprite_begin(0, 0, VW, VH);

	switch (gstate) {
	case ST_TITLE:
		draw_title();
		break;

	case ST_PLAY:
	case ST_PAUSE:
		draw_board();
		if (gstate == ST_PLAY) {
			draw_ghost();
			draw_piece(&cur, px, py, FX, FY, 1.0f);
		}
		draw_panel();
		if (gstate == ST_PAUSE) {
			static const char *pause_items[] = {
				"Continue", "Quit to Title", "Exit"
			};
			int i;

			lud_sprite_rect(0, 0, VW, VH, 0, 0, 0, 0.5f);
			lud_draw_text_centered(font, VW / 2, VH / 2 - 40, 2,
			                       "PAUSED");
			/* menu box */
			lud_sprite_rect(VW / 2 - 70, VH / 2 - 18,
			                140, 60, 0, 0, 0, 0.7f);
			lud_sprite_rect_lines(VW / 2 - 70, VH / 2 - 18,
			                      140, 60, 0.5f, 0.5f, 0.6f, 1.0f);
			for (i = 0; i < 3; i++) {
				float y = (float)(VH / 2 - 10 + i * 16);

				if (i == pause_sel)
					lud_draw_text(font, VW / 2 - 55, y, 1, ">");
				lud_draw_text(font, VW / 2 - 42, y, 1,
				              pause_items[i]);
			}
		}
		break;

	case ST_OVER:
		draw_board();
		draw_panel();
		lud_sprite_rect(0, 0, VW, VH, 0, 0, 0, 0.6f);
		lud_draw_text_centered(font, VW / 2, VH / 2 - 16, 2,
		                       "GAME OVER");
		{
			char buf[32] = "Score: ";
			istr(buf + 7, score);
			lud_draw_text_centered(font, VW / 2, VH / 2 + 10, 1,
			                       buf);
		}
		lud_draw_text_centered(font, VW / 2, VH / 2 + 30, 1,
		                       "Press ENTER to continue");
		break;
	}

	lud_sprite_end();
}

static void
cleanup(void)
{
	lud_destroy_font(font);
}

int
main(int argc, char **argv)
{
	return lud_run(&(lud_desc_t){
		.app_name = "tridrop",
		.width = 640,
		.height = 480,
		.resizable = 1,
		.argc = argc,
		.argv = argv,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.event = on_event,
	});
}
