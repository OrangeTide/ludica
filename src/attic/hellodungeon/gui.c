/* gui.c */
#include "gui.h"
#include "state.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui/cimgui.h>
#include <initgl.h>

void
gui_init(void)
{
	simgui_desc_t gui_desc = { 0 };
	simgui_setup(&gui_desc);
}

void
gui_done(void)
{
	simgui_shutdown();
}

void
gui_frame(void)
{
	const int canvas_width = sapp_width();
	const int canvas_height = sapp_height();
	simgui_frame_desc_t frame_desc = {
		.width = canvas_width,
		.height = canvas_height,
		.delta_time = sapp_frame_duration(),
		.dpi_scale = sapp_dpi_scale(),
	};

	simgui_new_frame(&frame_desc);

	static float f = 0.0f;
	igText("Hello GUI");
	igSliderFloat("float", &f, 0.0f, 1.0f, "%.3f", ImGuiScrollFlags_None);
	igColorEdit3("clear color", (float*)&state.gfx.pass_action.colors[0].value, 0);

	igRadioButton_Bool("Up", state.input.action_down[INPUT_ACTION_UP]);
	igRadioButton_Bool("Down", state.input.action_down[INPUT_ACTION_DOWN]);
	igRadioButton_Bool("Left", state.input.action_down[INPUT_ACTION_LEFT]);
	igRadioButton_Bool("Right", state.input.action_down[INPUT_ACTION_RIGHT]);

	if (igButton("another Window", (ImVec2) { 0.0f, 0.0f })) {
		        state.gui.show_another_window ^= 1;
	}

	igText("application average %.3f ms/frame (%.1f FPS)", 1000.0f / igGetIO()->Framerate, igGetIO()->Framerate);

	if (state.gui.show_another_window) {
		igSetNextWindowSize((ImVec2){100, 100}, ImGuiCond_FirstUseEver);
		igBegin("Another Window", &state.gui.show_another_window, 0);
		igText("Okay");
		igEnd();
	}

	sg_begin_default_pass(&state.gui.pass_gui, canvas_width, canvas_height);
	simgui_render();
	sg_end_pass();
}

bool
gui_event(const sapp_event *e)
{
	return simgui_handle_event(e);
}
