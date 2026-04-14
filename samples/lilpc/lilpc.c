/* lilpc.c - 286 XT emulator main integration */
#include "lilpc.h"
#include <ludica.h>
#include <ludica_gfx.h>
#include <ludica_input.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

static lilpc_t pc;
static lud_texture_t screen_tex;
static lud_shader_t crt_shader;
static lud_mesh_t crt_quad;
static int tex_w, tex_h;

/* ======================================================================== */
/* CRT display shader (GLES3)                                               */
/* ======================================================================== */

static const char crt_vert_src[] =
	"#version 300 es\n"
	"in vec4 a_pos;\n"
	"out vec2 v_uv;\n"
	"void main() {\n"
	"    v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);\n"
	"    gl_Position = a_pos;\n"
	"}\n";

static const char crt_frag_src[] =
	"#version 300 es\n"
	"precision mediump float;\n"
	"in vec2 v_uv;\n"
	"out vec4 frag_color;\n"
	"\n"
	"uniform sampler2D u_screen;\n"
	"uniform vec4 u_palette[16];\n"
	"uniform vec2 u_src_size;\n"    /* source texture size (e.g. 640, 200) */
	"uniform vec2 u_tex_size;\n"    /* allocated texture size (720, 350) */
	"uniform int u_border_idx;\n"   /* border palette index */
	"uniform float u_curvature;\n"  /* barrel distortion strength */
	"\n"
	"const float OVERSCAN = 0.92;\n"  /* visible area fraction */
	"\n"
	"vec2 barrel(vec2 uv, float k) {\n"
	"    vec2 c = uv - 0.5;\n"
	"    float r2 = dot(c, c);\n"
	"    return uv + c * r2 * k;\n"
	"}\n"
	"\n"
	"vec4 fetch(vec2 uv) {\n"
	"    /* map from display UV to texture UV (handle sub-allocation) */\n"
	"    vec2 tex_uv = uv * u_src_size / u_tex_size;\n"
	"    float idx = texture(u_screen, tex_uv).r * 255.0 + 0.5;\n"
	"    return u_palette[int(idx) & 15];\n"
	"}\n"
	"\n"
	"void main() {\n"
	"    /* barrel distortion */\n"
	"    vec2 uv = barrel(v_uv, u_curvature);\n"
	"\n"
	"    /* outside the curved screen = black (monitor housing) */\n"
	"    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
	"        frag_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
	"        return;\n"
	"    }\n"
	"\n"
	"    /* overscan: map the visible area to the center portion */\n"
	"    vec2 os = (uv - 0.5) / OVERSCAN + 0.5;\n"
	"\n"
	"    /* outside active area = border color */\n"
	"    if (os.x < 0.0 || os.x > 1.0 || os.y < 0.0 || os.y > 1.0) {\n"
	"        frag_color = u_palette[u_border_idx];\n"
	"        /* apply vignette to border too */\n"
	"        vec2 vig = uv * (1.0 - uv);\n"
	"        frag_color.rgb *= clamp(pow(vig.x * vig.y * 15.0, 0.25), 0.0, 1.0);\n"
	"        frag_color.a = 1.0;\n"
	"        return;\n"
	"    }\n"
	"\n"
	"    /* scanline phosphor glow.\n"
	"     * Each source texel maps to ~2 output pixels vertically (200->400).\n"
	"     * At the center of a source scanline: sharp, no blur.\n"
	"     * Between scanlines: blend from neighbors with exponential falloff\n"
	"     * so the glow tapers quickly — phosphor light, not vaseline. */\n"
	"    float src_y = os.y * u_src_size.y;\n"
	"    float texel_h = 1.0 / u_src_size.y;\n"
	"    float frac_y = fract(src_y);  /* 0=scanline center, 0.5=gap */\n"
	"    float dist = abs(frac_y - 0.5) * 2.0; /* 1=center, 0=gap */\n"
	"\n"
	"    vec4 center = fetch(os);\n"
	"    /* only blend when away from scanline center */\n"
	"    float blur_amt = 1.0 - dist * dist; /* quadratic: sharp center */\n"
	"    vec4 above = fetch(vec2(os.x, clamp(os.y - texel_h, 0.0, 1.0)));\n"
	"    vec4 below = fetch(vec2(os.x, clamp(os.y + texel_h, 0.0, 1.0)));\n"
	"    vec4 glow = (above + below) * 0.5;\n"
	"    vec4 color = mix(center, glow, blur_amt * 0.5);\n"
	"\n"
	"    /* scanline darkening: exponential falloff from center */\n"
	"    float scanline = 0.42 + 0.58 * dist * dist;\n"
	"    color.rgb *= scanline;\n"
	"\n"
	"    /* vignette */\n"
	"    vec2 vig = uv * (1.0 - uv);\n"
	"    float v = clamp(pow(vig.x * vig.y * 15.0, 0.25), 0.0, 1.0);\n"
	"    color.rgb *= v;\n"
	"\n"
	"    frag_color = vec4(color.rgb, 1.0);\n"
	"}\n";

/* fullscreen quad vertices: x, y, z, w */
static const float crt_quad_verts[] = {
	-1.0f, -1.0f, 0.0f, 1.0f,
	 1.0f, -1.0f, 0.0f, 1.0f,
	 1.0f,  1.0f, 0.0f, 1.0f,
	-1.0f,  1.0f, 0.0f, 1.0f,
};

/*
 * Map ludica keycodes to XT scan code set 1 (make codes).
 * Break code = make code | 0x80.
 */
static uint8_t keycode_to_scancode(enum lud_keycode key)
{
	switch (key) {
	case LUD_KEY_ESCAPE:        return 0x01;
	case LUD_KEY_1:             return 0x02;
	case LUD_KEY_2:             return 0x03;
	case LUD_KEY_3:             return 0x04;
	case LUD_KEY_4:             return 0x05;
	case LUD_KEY_5:             return 0x06;
	case LUD_KEY_6:             return 0x07;
	case LUD_KEY_7:             return 0x08;
	case LUD_KEY_8:             return 0x09;
	case LUD_KEY_9:             return 0x0A;
	case LUD_KEY_0:             return 0x0B;
	case LUD_KEY_MINUS:         return 0x0C;
	case LUD_KEY_EQUAL:         return 0x0D;
	case LUD_KEY_BACKSPACE:     return 0x0E;
	case LUD_KEY_TAB:           return 0x0F;
	case LUD_KEY_Q:             return 0x10;
	case LUD_KEY_W:             return 0x11;
	case LUD_KEY_E:             return 0x12;
	case LUD_KEY_R:             return 0x13;
	case LUD_KEY_T:             return 0x14;
	case LUD_KEY_Y:             return 0x15;
	case LUD_KEY_U:             return 0x16;
	case LUD_KEY_I:             return 0x17;
	case LUD_KEY_O:             return 0x18;
	case LUD_KEY_P:             return 0x19;
	case LUD_KEY_LEFT_BRACKET:  return 0x1A;
	case LUD_KEY_RIGHT_BRACKET: return 0x1B;
	case LUD_KEY_ENTER:         return 0x1C;
	case LUD_KEY_LEFT_CONTROL:
	case LUD_KEY_RIGHT_CONTROL: return 0x1D;
	case LUD_KEY_A:             return 0x1E;
	case LUD_KEY_S:             return 0x1F;
	case LUD_KEY_D:             return 0x20;
	case LUD_KEY_F:             return 0x21;
	case LUD_KEY_G:             return 0x22;
	case LUD_KEY_H:             return 0x23;
	case LUD_KEY_J:             return 0x24;
	case LUD_KEY_K:             return 0x25;
	case LUD_KEY_L:             return 0x26;
	case LUD_KEY_SEMICOLON:     return 0x27;
	case LUD_KEY_APOSTROPHE:    return 0x28;
	case LUD_KEY_GRAVE_ACCENT:  return 0x29;
	case LUD_KEY_LEFT_SHIFT:    return 0x2A;
	case LUD_KEY_BACKSLASH:     return 0x2B;
	case LUD_KEY_Z:             return 0x2C;
	case LUD_KEY_X:             return 0x2D;
	case LUD_KEY_C:             return 0x2E;
	case LUD_KEY_V:             return 0x2F;
	case LUD_KEY_B:             return 0x30;
	case LUD_KEY_N:             return 0x31;
	case LUD_KEY_M:             return 0x32;
	case LUD_KEY_COMMA:         return 0x33;
	case LUD_KEY_PERIOD:        return 0x34;
	case LUD_KEY_SLASH:         return 0x35;
	case LUD_KEY_RIGHT_SHIFT:   return 0x36;
	case LUD_KEY_KP_MULTIPLY:   return 0x37;
	case LUD_KEY_LEFT_ALT:
	case LUD_KEY_RIGHT_ALT:     return 0x38;
	case LUD_KEY_SPACE:         return 0x39;
	case LUD_KEY_CAPS_LOCK:     return 0x3A;
	case LUD_KEY_F1:            return 0x3B;
	case LUD_KEY_F2:            return 0x3C;
	case LUD_KEY_F3:            return 0x3D;
	case LUD_KEY_F4:            return 0x3E;
	case LUD_KEY_F5:            return 0x3F;
	case LUD_KEY_F6:            return 0x40;
	case LUD_KEY_F7:            return 0x41;
	case LUD_KEY_F8:            return 0x42;
	case LUD_KEY_F9:            return 0x43;
	case LUD_KEY_F10:           return 0x44;
	case LUD_KEY_NUM_LOCK:      return 0x45;
	case LUD_KEY_SCROLL_LOCK:   return 0x46;
	case LUD_KEY_HOME:          return 0x47;
	case LUD_KEY_UP:            return 0x48;
	case LUD_KEY_PAGE_UP:       return 0x49;
	case LUD_KEY_KP_SUBTRACT:   return 0x4A;
	case LUD_KEY_LEFT:          return 0x4B;
	case LUD_KEY_KP_5:          return 0x4C;
	case LUD_KEY_RIGHT:         return 0x4D;
	case LUD_KEY_KP_ADD:        return 0x4E;
	case LUD_KEY_END:           return 0x4F;
	case LUD_KEY_DOWN:          return 0x50;
	case LUD_KEY_PAGE_DOWN:     return 0x51;
	case LUD_KEY_INSERT:        return 0x52;
	case LUD_KEY_DELETE:        return 0x53;
	default:                    return 0x00; /* unmapped */
	}
}

/*
 * BIOS data area initialization.
 * The BIOS normally sets these up, but we seed a few critical values
 * so the BIOS can find hardware during POST.
 */
static void setup_bda(lilpc_t *pc)
{
	bus_t *bus = &pc->bus;

	/* equipment word at 0040:0010 (INT 11h)
	 * Bits 0: floppy installed
	 * Bits 1: 8087 present (0 = no)
	 * Bits 2-3: system board RAM (11 = 64KB base)
	 * Bits 4-5: initial video mode (10 = 80-col color)
	 * Bits 6-7: number of floppy drives - 1 (00 = 1 drive)
	 */
	uint16_t equip = 0x0001; /* 1 floppy, no 8087 */
	if (pc->video.hercules)
		equip |= 0x0030; /* 30h = MDA 80-col */
	else
		equip |= 0x0020; /* 20h = CGA 80-col */
	bus_write16(bus, 0x410, equip);

	/* conventional memory size in KB at 0040:0013 */
	bus_write16(bus, 0x413, (uint16_t)pc->ram_kb);

	/* COM port base addresses at 0040:0000 */
	bus_write16(bus, 0x400, 0x03F8); /* COM1 */
	bus_write16(bus, 0x402, 0x02F8); /* COM2 */

	/* LPT port base addresses at 0040:0008 */
	bus_write16(bus, 0x408, 0x0378); /* LPT1 */

	/* keyboard buffer pointers at 0040:001A/001C (head/tail) */
	bus_write16(bus, 0x41A, 0x001E); /* buffer head */
	bus_write16(bus, 0x41C, 0x001E); /* buffer tail */

	/* keyboard buffer start/end at 0040:0080/0082 */
	bus_write16(bus, 0x480, 0x001E);
	bus_write16(bus, 0x482, 0x003E);
}

int lilpc_init(lilpc_t *lpc, int ram_kb, bool hercules,
	const char *bios_path, const char *disk_path)
{
	memset(lpc, 0, sizeof(*lpc));
	lpc->ram_kb = ram_kb;

	/* init subsystems in dependency order */
	if (bus_init(&lpc->bus, ram_kb) != 0)
		return -1;

	cpu286_init(&lpc->cpu);

	/* register I/O devices before loading ROM (some BIOSes probe ports early) */
	pic_init(&lpc->pic, lpc);
	pit_init(&lpc->pit, lpc);
	dma_init(&lpc->dma, lpc);
	video_init(&lpc->video, lpc, hercules);
	kbd_init(&lpc->kbd, lpc);

	/*
	 * Configure XT DIP switches (read via port 62h).
	 * Port 61h bit 2 selects which bank port 62h returns:
	 *   Low bank (bit 2 clear): expansion RAM config (bits 0-3)
	 *   High bank (bit 2 set):  video mode (bits 2-3), floppy count-1 (bits 0-1)
	 *
	 * The BIOS reads both banks, combines them:
	 *   equip_byte = (high_bank << 4) | (low_bank & 0x0F)
	 * Then sets additional bits (floppy present, etc.) later.
	 */
	{
		/* low bank: expansion memory (not critical, BIOS counts RAM) */
		lpc->kbd.dip_sw_lo = 0x0F; /* max expansion memory */

		/* high bank: shifted left 4 to become equip bits 4-7 */
		/* high bank bits 0-1 → equip bits 4-5: video mode */
		/*   00=reserved, 01=CGA 40-col, 10=CGA 80-col, 11=MDA */
		/* high bank bits 2-3 → equip bits 6-7: floppy drives - 1 */
		uint8_t video_sw = hercules ? 0x03 : 0x02; /* MDA or CGA 80-col */
		uint8_t floppy_sw = 0x00; /* 1 floppy drive (0 = count-1) */
		lpc->kbd.dip_sw_hi = (floppy_sw << 2) | video_sw;
	}

	fdc_init(&lpc->fdc, lpc);
	uart_init(&lpc->com1, lpc, 0x03F8, 4);
	uart_init(&lpc->com2, lpc, 0x02F8, 3);
	lpt_init(&lpc->lpt1, lpc, 0x0378, 7);
	speaker_init(&lpc->speaker);

	/* load BIOS ROM */
	if (bus_load_rom(&lpc->bus, bios_path) != 0) {
		bus_cleanup(&lpc->bus);
		return -1;
	}

	/* load floppy disk image if provided */
	if (disk_path && disk_path[0]) {
		if (fdc_load_image(&lpc->fdc, 0, disk_path) != 0)
			fprintf(stderr, "lilpc: warning: could not load disk image\n");
	}

	/* seed BIOS data area */
	setup_bda(lpc);

	/* reset CPU to start execution at FFFF:0000 */
	cpu286_reset(&lpc->cpu);

	return 0;
}

void lilpc_cleanup(lilpc_t *lpc)
{
	for (int i = 0; i < FDC_MAX_DRIVES; i++)
		free(lpc->fdc.drive[i].data);
	bus_cleanup(&lpc->bus);
}

void lilpc_run_frame(lilpc_t *lpc)
{
	debugmon_t *dm = &lpc->debugmon;
	uint64_t frame_cycles = LILPC_CYCLES_PER_FRAME;
	uint64_t start = lpc->cpu.cycles;

	while (lpc->cpu.cycles - start < frame_cycles) {
		/* check for pending interrupts */
		if (lpc->cpu.irq_pending && (lpc->cpu.flags & FLAG_IF) &&
		    !lpc->cpu.halted) {
			int vec = pic_get_interrupt(&lpc->pic);
			if (vec >= 0) {
				cpu286_interrupt(lpc, vec);
				lpc->cpu.irq_pending = false;
			}
		}

		if (lpc->cpu.halted) {
			/* advance time to next timer tick (fast-forward HLT) */
			lpc->cpu.cycles += 16;
			pit_tick(&lpc->pit, lpc, lpc->cpu.cycles);
			pic_tick(&lpc->pic, lpc);
			if (lpc->cpu.irq_pending)
				lpc->cpu.halted = false;
			continue;
		}

		/* check breakpoints before executing */
		if (dm->bp_count > 0 &&
		    debugmon_check_bp(dm, lpc->cpu.seg[SEG_CS].sel, lpc->cpu.ip)) {
			dm->paused = true;
			dm->step_one = false;
			dm_notify_break(dm, lpc);
			break;
		}

		/* execute one instruction */
		int cycles = cpu286_step(lpc);
		(void)cycles;

		/* single-step: pause after one instruction */
		if (dm->step_one) {
			dm->paused = true;
			dm->step_one = false;
			dm_notify_step(dm, lpc);
			break;
		}

		/* tick PIT periodically (every ~100 CPU cycles to avoid overhead) */
		if ((lpc->cpu.cycles & 0x3F) == 0) {
			pit_tick(&lpc->pit, lpc, lpc->cpu.cycles);
			pic_tick(&lpc->pic, lpc);
		}
	}

	lpc->total_cycles = lpc->cpu.cycles;
}

void lilpc_reset(lilpc_t *lpc)
{
	cpu286_reset(&lpc->cpu);
	lpc->cpu.halted = false;

	/* re-seed BDA */
	setup_bda(lpc);
}

/* ======================================================================== */
/* ludica frontend                                                          */
/* ======================================================================== */

static char bios_path[512];
static char disk_path[512];
static bool use_hercules;
static uint64_t debug_flags;
static int debug_port;

static int parse_args(void);

static void init(void)
{
	int ram = 640;

	if (parse_args() != 0) {
		lud_quit();
		return;
	}

	if (lilpc_init(&pc, ram, use_hercules, bios_path, disk_path) != 0) {
		fprintf(stderr, "lilpc: init failed\n");
		lud_quit();
		return;
	}

	/* create display texture (single-channel palette indices) */
	video_get_size(&pc.video, &tex_w, &tex_h);
	screen_tex = lud_make_texture(&(lud_texture_desc_t){
		.width = VIDEO_MAX_W,
		.height = VIDEO_MAX_H,
		.format = LUD_PIXFMT_R8,
		.min_filter = LUD_FILTER_NEAREST,
		.mag_filter = LUD_FILTER_NEAREST,
	});

	/* CRT shader and fullscreen quad */
	crt_shader = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = crt_vert_src,
		.frag_src = crt_frag_src,
		.attrs = { "a_pos" },
		.num_attrs = 1,
	});
	crt_quad = lud_make_mesh(&(lud_mesh_desc_t){
		.vertices = crt_quad_verts,
		.vertex_count = 4,
		.vertex_stride = 16,
		.layout = { { .size = 4, .offset = 0 } },
		.num_attrs = 1,
		.primitive = LUD_PRIM_TRIANGLE_FAN,
		.usage = LUD_USAGE_STATIC,
	});

	pc.debug = debug_flags;

	if (debug_port > 0) {
		if (debugmon_init(&pc.debugmon, debug_port) != 0) {
			fprintf(stderr, "lilpc: failed to start debug monitor\n");
			lud_quit();
			return;
		}
	}

	fprintf(stderr, "lilpc: 286 XT emulator started (%d KB RAM, %s)\n",
		ram, use_hercules ? "Hercules" : "CGA");
}

static void frame(float dt)
{
	(void)dt;

	/* poll debug monitor for commands */
	bool should_run = debugmon_poll(&pc.debugmon, &pc);

	/* run one frame of CPU execution */
	if (should_run)
		lilpc_run_frame(&pc);

	/* render video to pixel buffer */
	video_render(&pc.video, &pc);
	video_get_size(&pc.video, &tex_w, &tex_h);

	/* upload palette-indexed pixels to GPU texture */
	lud_update_texture(screen_tex, 0, 0, tex_w, tex_h, pc.video.pixels);

	/* draw to screen via CRT shader */
	int win_w = lud_width();
	int win_h = lud_height();

	/* fit display to window maintaining 4:3 (CGA) or native (Herc) aspect */
	float src_aspect = pc.video.hercules
		? (float)tex_w / (float)tex_h
		: 4.0f / 3.0f;
	float dst_aspect = (float)win_w / (float)win_h;
	int vp_x, vp_y, vp_w, vp_h;

	if (src_aspect > dst_aspect) {
		vp_w = win_w;
		vp_h = (int)((float)win_w / src_aspect);
		vp_x = 0;
		vp_y = (win_h - vp_h) / 2;
	} else {
		vp_h = win_h;
		vp_w = (int)((float)win_h * src_aspect);
		vp_x = (win_w - vp_w) / 2;
		vp_y = 0;
	}

	lud_viewport(0, 0, win_w, win_h);
	lud_clear(0.0f, 0.0f, 0.0f, 1.0f);
	lud_viewport(vp_x, vp_y, vp_w, vp_h);

	/* build palette uniform from RGBA palette table */
	const uint32_t *pal = pc.video.hercules ? herc_palette : cga_palette;
	int pal_count = pc.video.hercules ? 2 : 16;
	float pal_uniform[16 * 4];
	memset(pal_uniform, 0, sizeof(pal_uniform));
	for (int i = 0; i < pal_count; i++) {
		uint32_t c = pal[i];
		pal_uniform[i * 4 + 0] = (float)((c >>  0) & 0xFF) / 255.0f;
		pal_uniform[i * 4 + 1] = (float)((c >>  8) & 0xFF) / 255.0f;
		pal_uniform[i * 4 + 2] = (float)((c >> 16) & 0xFF) / 255.0f;
		pal_uniform[i * 4 + 3] = 1.0f;
	}

	lud_apply_shader(crt_shader);
	lud_bind_texture(screen_tex, 0);
	lud_uniform_int(crt_shader, "u_screen", 0);
	lud_uniform_vec2(crt_shader, "u_src_size", (float)tex_w, (float)tex_h);
	lud_uniform_vec2(crt_shader, "u_tex_size",
		(float)VIDEO_MAX_W, (float)VIDEO_MAX_H);
	lud_uniform_int(crt_shader, "u_border_idx",
		pc.video.hercules ? 0 : pc.video.border_color);
	lud_uniform_float(crt_shader, "u_curvature",
		pc.video.hercules ? 0.1f : 0.15f);

	/* upload palette as individual vec4 uniforms */
	for (int i = 0; i < 16; i++) {
		char name[16];
		snprintf(name, sizeof(name), "u_palette[%d]", i);
		lud_uniform_vec4(crt_shader, name,
			pal_uniform[i*4+0], pal_uniform[i*4+1],
			pal_uniform[i*4+2], pal_uniform[i*4+3]);
	}

	lud_draw(crt_quad);

	/* drain serial output to stderr (debug console) */
	while (uart_has_output(&pc.com1)) {
		uint8_t ch = uart_read_output(&pc.com1);
		fputc(ch, stderr);
	}
}

static int on_event(const lud_event_t *ev)
{
	switch (ev->type) {
	case LUD_EV_KEY_DOWN:
	{
		/* F11 = reset */
		if (ev->key.keycode == LUD_KEY_F11) {
			lilpc_reset(&pc);
			return 1;
		}
		/* F12 = toggle CPU trace */
		if (ev->key.keycode == LUD_KEY_F12) {
			pc.debug ^= DBG_CPU;
			fprintf(stderr, "lilpc: CPU trace %s\n",
				(pc.debug & DBG_CPU) ? "ON" : "OFF");
			return 1;
		}

		uint8_t sc = keycode_to_scancode(ev->key.keycode);
		if (sc)
			kbd_press(&pc.kbd, &pc, sc);
		return 1;
	}
	case LUD_EV_KEY_UP:
	{
		uint8_t sc = keycode_to_scancode(ev->key.keycode);
		if (sc)
			kbd_release(&pc.kbd, &pc, sc);
		return 1;
	}
	default:
		break;
	}
	return 0;
}

static void dump_textbuf(void)
{
	/*
	 * Dump CGA text buffer at B800:0000 as plain text.
	 * 80x25 (4000 bytes), character at even offsets, attribute at odd.
	 * Also dump the full 4 pages (80x100) for 8KB buffer.
	 */
	FILE *f = fopen("/tmp/lilpc_textbuf.txt", "w");
	if (!f) return;

	uint32_t base = 0xB8000;
	int cols = 80;
	int rows = 100; /* 4 pages: 8KB / (80*2) = 50, but cover full 16KB */

	for (int row = 0; row < rows; row++) {
		int last_nonspace = -1;
		for (int col = 0; col < cols; col++) {
			uint32_t addr = base + (row * cols + col) * 2;
			uint8_t ch = pc.bus.ram[addr];
			if (ch == 0) ch = ' ';
			if (ch != ' ') last_nonspace = col;
		}
		for (int col = 0; col <= last_nonspace; col++) {
			uint32_t addr = base + (row * cols + col) * 2;
			uint8_t ch = pc.bus.ram[addr];
			if (ch == 0) ch = ' ';
			if (ch < 0x20 || ch == 0x7F) ch = '.';
			fputc(ch, f);
		}
		fputc('\n', f);
	}

	fclose(f);
	fprintf(stderr, "\nlilpc: text buffer dumped to /tmp/lilpc_textbuf.txt\n");
}

static void dump_cpu_state(void)
{
	cpu286_t *cpu = &pc.cpu;
	fprintf(stderr, "\nlilpc: CPU state at exit:\n");
	fprintf(stderr, "  CS:IP = %04X:%04X  flags=%04X  halted=%d\n",
		cpu->seg[SEG_CS].sel, cpu->ip, cpu->flags, cpu->halted);
	fprintf(stderr, "  AX=%04X BX=%04X CX=%04X DX=%04X\n",
		cpu->ax, cpu->bx, cpu->cx, cpu->dx);
	fprintf(stderr, "  SP=%04X BP=%04X SI=%04X DI=%04X\n",
		cpu->sp, cpu->bp, cpu->si, cpu->di);
	fprintf(stderr, "  DS=%04X ES=%04X SS=%04X\n",
		cpu->seg[SEG_DS].sel, cpu->seg[SEG_ES].sel, cpu->seg[SEG_SS].sel);
	fprintf(stderr, "  video mode_ctrl=%02X\n", pc.video.mode_ctrl);

	/* dump key BDA fields */
	uint8_t *bda = pc.bus.ram + 0x400;
	fprintf(stderr, "  BDA: vid_mode=%02X crt_port=%04X cols=%04X "
		"regen_sz=%04X regen_off=%04X cursor=%04X\n",
		bda[0x49], bda[0x63] | (bda[0x64] << 8),
		bda[0x4A] | (bda[0x4B] << 8),
		bda[0x4C] | (bda[0x4D] << 8),
		bda[0x4E] | (bda[0x4F] << 8),
		bda[0x50] | (bda[0x51] << 8));
	fprintf(stderr, "  BDA: equip=%04X mem_kb=%04X mode_ctrl=%02X palette=%02X\n",
		bda[0x10] | (bda[0x11] << 8),
		bda[0x13] | (bda[0x14] << 8),
		bda[0x65], bda[0x66]);
}

static void signal_handler(int sig)
{
	(void)sig;
	_exit(0);
}

static void cleanup(void)
{
	if (pc.debug & DBG_EXIT) {
		dump_textbuf();
		dump_cpu_state();
	}
	debugmon_cleanup(&pc.debugmon);
	lud_destroy_mesh(crt_quad);
	lud_destroy_shader(crt_shader);
	lud_destroy_texture(screen_tex);
	lilpc_cleanup(&pc);
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s --bios=<rom> [--disk=<image>] [--herc]\n", prog);
	fprintf(stderr, "  --bios=<path>       BIOS ROM file (required)\n");
	fprintf(stderr, "  --disk=<path>       Floppy disk image\n");
	fprintf(stderr, "  --herc              Use Hercules display (default: CGA)\n");
	fprintf(stderr, "  --debug=<flags>     Enable debug output (or set LILPC_DEBUG env)\n");
	fprintf(stderr, "        a=CPU b=FDC c=DMA d=PIC e=PIT f=exit-dump\n");
	fprintf(stderr, "  --debug-port=<port> TCP debug monitor port\n");
}

static int
parse_args(void)
{
	const char *val;

	bios_path[0] = 0;
	disk_path[0] = 0;
	use_hercules = false;
	debug_flags = 0;
	debug_port = 0;

	if ((val = lud_get_config("bios")))
		snprintf(bios_path, sizeof(bios_path), "%s", val);
	if ((val = lud_get_config("disk")))
		snprintf(disk_path, sizeof(disk_path), "%s", val);
	if (lud_get_config("herc"))
		use_hercules = true;
	if ((val = lud_get_config("debug")))
		debug_flags = dbg_parse(val);
	/* also check LILPC_DEBUG environment variable */
	if (!debug_flags) {
		const char *env = getenv("LILPC_DEBUG");
		if (env)
			debug_flags = dbg_parse(env);
	}
	if ((val = lud_get_config("debug-port")))
		debug_port = atoi(val);
	if (lud_get_config("help")) {
		usage(lud_get_config("_program"));
		return -1;
	}

	if (!bios_path[0]) {
		fprintf(stderr, "Error: --bios is required\n");
		usage(lud_get_config("_program"));
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);

	return lud_run(&(lud_desc_t){
		.app_name = "lilpc",
		.width = 800,
		.height = 600,
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
