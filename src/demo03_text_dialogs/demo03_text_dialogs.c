/*
 * demo03_text_dialogs — Bitmap font text rendering and dialog boxes.
 *
 * Demonstrates initgl's text rendering, rectangle primitives, and
 * a multi-page dialog box system.
 *
 * Enter/Space: advance dialog or select menu item
 * Up/Down: navigate menu
 * Escape: quit
 */

#include "initgl.h"
#include "initgl_gfx.h"
#include "initgl_font.h"
#include <math.h>

#define VIRTUAL_W  320
#define VIRTUAL_H  180

/* Scenes */
enum scene { SCENE_TITLE, SCENE_DIALOG, SCENE_MENU };

static initgl_font_t font;
static enum scene scene;
static float time_acc;

/* Dialog state */
static const char *dialog_pages[] = {
	"Welcome, traveler! This village has been troubled by strange happenings lately.",
	"The old well in the town square... something stirs within its depths at night.",
	"If you are brave enough, please investigate. The villagers will reward you handsomely.",
	"Take this lantern. You will need it in the darkness below.",
};
#define DIALOG_PAGE_COUNT ((int)(sizeof(dialog_pages) / sizeof(dialog_pages[0])))
static int dialog_page;

/* Menu state */
static const char *menu_items[] = { "Resume", "Options", "Quit" };
#define MENU_ITEM_COUNT ((int)(sizeof(menu_items) / sizeof(menu_items[0])))
static int menu_sel;

/* Dialog box dimensions */
#define DIALOG_X      10
#define DIALOG_Y      123
#define DIALOG_W      300
#define DIALOG_H      44
#define DIALOG_PAD    8
#define DIALOG_LINE_H 10

static void
draw_title(void)
{
	float blink;

	initgl_draw_text_centered(font, VIRTUAL_W / 2, 50, 2, "Text Demo");
	initgl_draw_text_centered(font, VIRTUAL_W / 2, 80, 1, "Bitmap Font & Dialog Boxes");

	/* blinking prompt — sine wave alpha, 2 Hz */
	blink = (sinf(time_acc * 2.0f * 3.14159f * 2.0f) + 1.0f) * 0.5f;
	if (blink > 0.3f)
		initgl_draw_text_centered(font, VIRTUAL_W / 2, 120, 1, "Press Enter");

	/* key hints at bottom */
	initgl_draw_text(font, 4, VIRTUAL_H - 12, 1, "Enter=dialog  M=menu  Esc=quit");
}

static void
draw_dialog(void)
{
	float blink;

	/* scene label */
	initgl_draw_text(font, 4, 4, 1, "Dialog Mode");

	/* dialog box background */
	initgl_sprite_rect(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H,
	                   0.0f, 0.0f, 0.0f, 0.85f);
	/* border */
	initgl_sprite_rect_lines(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H,
	                         1.0f, 1.0f, 1.0f, 1.0f);

	/* word-wrapped text */
	initgl_draw_text_wrapped(font,
	                         DIALOG_X + DIALOG_PAD,
	                         DIALOG_Y + DIALOG_PAD / 2 + 2,
	                         1,
	                         DIALOG_W - DIALOG_PAD * 2,
	                         DIALOG_LINE_H,
	                         dialog_pages[dialog_page]);

	/* blinking advance indicator */
	blink = (sinf(time_acc * 2.0f * 3.14159f * 2.0f) + 1.0f) * 0.5f;
	if (blink > 0.3f)
		initgl_draw_text(font,
		                 DIALOG_X + DIALOG_W - DIALOG_PAD - 8,
		                 DIALOG_Y + DIALOG_H - 10,
		                 1, ">");

	/* page indicator */
	{
		char buf[32];
		int len;
		len = 0;
		buf[len++] = '0' + (dialog_page + 1);
		buf[len++] = '/';
		buf[len++] = '0' + DIALOG_PAGE_COUNT;
		buf[len] = '\0';
		initgl_draw_text(font, DIALOG_X + DIALOG_PAD, DIALOG_Y + DIALOG_H - 10, 1, buf);
	}

	initgl_draw_text(font, 4, VIRTUAL_H - 12, 1, "Space/Enter=next  Esc=quit");
}

static void
draw_menu(void)
{
	float start_y;
	int i;

	initgl_draw_text_centered(font, VIRTUAL_W / 2, 40, 2, "Pause Menu");

	/* menu background */
	initgl_sprite_rect(80, 65, 160, 70, 0.0f, 0.0f, 0.0f, 0.75f);
	initgl_sprite_rect_lines(80, 65, 160, 70, 1.0f, 1.0f, 1.0f, 1.0f);

	start_y = 75.0f;
	for (i = 0; i < MENU_ITEM_COUNT; i++) {
		float y = start_y + (float)i * 18.0f;
		if (i == menu_sel) {
			initgl_draw_text(font, 95, y, 1, ">");
		}
		initgl_draw_text(font, 110, y, 1, menu_items[i]);
	}

	initgl_draw_text(font, 4, VIRTUAL_H - 12, 1, "Up/Down=select  Enter=choose  Esc=quit");
}

static int
on_event(const initgl_event_t *ev)
{
	if (ev->type != INITGL_EV_KEY_DOWN)
		return 0;

	if (ev->key.keycode == INITGL_KEY_ESCAPE) {
		initgl_quit();
		return 1;
	}

	switch (scene) {
	case SCENE_TITLE:
		if (ev->key.keycode == INITGL_KEY_ENTER) {
			scene = SCENE_DIALOG;
			dialog_page = 0;
		} else if (ev->key.keycode == INITGL_KEY_M) {
			scene = SCENE_MENU;
			menu_sel = 0;
		}
		return 1;

	case SCENE_DIALOG:
		if (ev->key.keycode == INITGL_KEY_ENTER ||
		    ev->key.keycode == INITGL_KEY_SPACE) {
			dialog_page++;
			if (dialog_page >= DIALOG_PAGE_COUNT) {
				dialog_page = 0;
				scene = SCENE_TITLE;
			}
		}
		return 1;

	case SCENE_MENU:
		if (ev->key.keycode == INITGL_KEY_UP) {
			menu_sel--;
			if (menu_sel < 0) menu_sel = MENU_ITEM_COUNT - 1;
		} else if (ev->key.keycode == INITGL_KEY_DOWN) {
			menu_sel++;
			if (menu_sel >= MENU_ITEM_COUNT) menu_sel = 0;
		} else if (ev->key.keycode == INITGL_KEY_ENTER) {
			if (menu_sel == 0) {
				/* Resume */
				scene = SCENE_TITLE;
			} else if (menu_sel == 2) {
				/* Quit */
				initgl_quit();
			}
		}
		return 1;
	}

	return 0;
}

static void
init(void)
{
	font = initgl_make_default_font();
	scene = SCENE_TITLE;
	time_acc = 0.0f;
}

static void
frame(float dt)
{
	time_acc += dt;

	initgl_viewport(0, 0, initgl_width(), initgl_height());
	initgl_clear(0.08f, 0.08f, 0.12f, 1.0f);

	initgl_sprite_begin(0, 0, VIRTUAL_W, VIRTUAL_H);

	switch (scene) {
	case SCENE_TITLE:  draw_title();  break;
	case SCENE_DIALOG: draw_dialog(); break;
	case SCENE_MENU:   draw_menu();   break;
	}

	/* FPS overlay */
	{
		char fps_buf[16];
		int fps, len;
		fps = (dt > 0.0f) ? (int)(1.0f / dt + 0.5f) : 0;
		/* manual int-to-string to avoid snprintf */
		len = 0;
		fps_buf[len++] = 'F';
		fps_buf[len++] = 'P';
		fps_buf[len++] = 'S';
		fps_buf[len++] = ':';
		if (fps >= 100) fps_buf[len++] = '0' + (fps / 100) % 10;
		if (fps >= 10)  fps_buf[len++] = '0' + (fps / 10) % 10;
		fps_buf[len++] = '0' + fps % 10;
		fps_buf[len] = '\0';
		initgl_draw_text(font, VIRTUAL_W - 8 * len - 2, 2, 1, fps_buf);
	}

	initgl_sprite_end();
}

static void
cleanup(void)
{
	initgl_destroy_font(font);
}

int
main(void)
{
	return initgl_run(&(initgl_desc_t){
		.app_name = "demo03 — text & dialogs",
		.width = 960,
		.height = 540,
		.resizable = 1,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.event = on_event,
	});
}
