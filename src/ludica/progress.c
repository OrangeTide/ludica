/*
 * progress.c — Loading progress bar.
 *
 * Draws a centered progress bar with an optional text label and swaps
 * buffers.  Designed to be called from within init() or frame() during
 * a synchronous loading sequence.  Manages its own font instance so
 * it works before the application has created any resources.
 */

#include "ludica_internal.h"
#include "ludica_gfx.h"
#include "ludica_font.h"
#include <GLES2/gl2.h>

/* Virtual resolution for progress screen layout */
#define PV_W  320.0f
#define PV_H  180.0f

/* Bar geometry (in virtual coords) */
#define BAR_W   200.0f
#define BAR_H   12.0f
#define BAR_X   ((PV_W - BAR_W) / 2.0f)
#define BAR_Y   ((PV_H - BAR_H) / 2.0f)

static lud_font_t progress_font;

void
lud_draw_progress(int step, int total, const char *label)
{
	float fraction;
	float fill_w;
	float label_x, label_y;

	if (total <= 0)
		total = 1;
	fraction = (float)step / (float)total;
	if (fraction < 0.0f) fraction = 0.0f;
	if (fraction > 1.0f) fraction = 1.0f;

	/* lazy-init font */
	if (progress_font.id == 0)
		progress_font = lud_make_default_font();

	lud_viewport(0, 0, lud__state.win_width, lud__state.win_height);
	lud_clear(0.05f, 0.05f, 0.08f, 1.0f);

	lud_sprite_begin(0, 0, PV_W, PV_H);

	/* bar background */
	lud_sprite_rect(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2,
	                0.25f, 0.25f, 0.3f, 1.0f);

	/* bar fill */
	fill_w = BAR_W * fraction;
	if (fill_w > 0.0f)
		lud_sprite_rect(BAR_X, BAR_Y, fill_w, BAR_H,
		                0.4f, 0.7f, 1.0f, 1.0f);

	/* label text centered above bar */
	if (label && *label) {
		label_x = PV_W / 2.0f;
		label_y = BAR_Y - 16.0f;
		lud_draw_text_centered(progress_font, label_x, label_y, 1, label);
	}

	lud_sprite_end();
	lud__platform_swap();
}
