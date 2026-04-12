/* graphics.c */
#include "state.h"
#include "graphics.h"
#include "screen.h"
#if defined(__ANDROID__)
#include "palette.glsl.android.h"
#elif defined(__EMSCRIPTEN__)
#include "palette.glsl.webgl.h"
#else
#include "palette.glsl.gl.h"
#endif

#include <initgl.h>

void
gfx_init(void)
{
	const sg_desc desc = {
		/*
		.buffer_pool_size = 2,
		.image_pool_size = 3,
		.shader_pool_size = 2,
		.pipeline_pool_size = 2,
		.pass_pool_size = 1,
		*/
		.context = sapp_sgcontext()
	};
	sg_setup(&desc);

	float quad_data[] = {
		-1.0,  1.0,  1.0,
		 1.0,  1.0,  1.0,
		-1.0, -1.0,  1.0,
		 1.0, -1.0,  1.0,
	};

	sg_buffer_desc quad_vbuffer = {
		.data = SG_RANGE(quad_data),
		.type = SG_BUFFERTYPE_VERTEXBUFFER,
		.usage = SG_USAGE_IMMUTABLE,
		.label = "quad-vertices",
	};
	state.gfx.bind.vertex_buffers[0] = sg_make_buffer(&quad_vbuffer);
	state.gfx.vertex_count = sizeof(quad_data) / sizeof(*quad_data) / (3);

	sg_image_desc screen_img = {
		.width = state.screen.width,
		.height = state.screen.height,
		.pixel_format = SG_PIXELFORMAT_R8,
		.usage = SG_USAGE_STREAM,
		.min_filter = SG_FILTER_LINEAR,
		.mag_filter = SG_FILTER_NEAREST,
		.wrap_u = SG_WRAP_CLAMP_TO_EDGE,
		.wrap_v = SG_WRAP_CLAMP_TO_EDGE,
		.label = "screen-texture",
	};
	sg_image img = sg_make_image(&screen_img);
	state.gfx.bind.fs_images[SLOT_demo3_screentexture] = img;

	sg_image_desc palette_desc = {
		.width = 256,
		.height = 1,
		.data.subimage[0][0] = SG_RANGE(state.screen.palette),
		.label = "palette-tex",
	};
	sg_image palette = sg_make_image(&palette_desc);
	state.gfx.bind.fs_images[SLOT_demo3_palette] = palette;

	sg_shader shd = sg_make_shader(demo3_palette_shader_desc(sg_query_backend()));

	sg_pipeline_desc pipeline_desc = {
		.shader = shd,
		.layout = {
			.attrs = {
				[ATTR_demo3_vs_position].format = SG_VERTEXFORMAT_FLOAT3,
			}
		},
		.cull_mode = SG_CULLMODE_BACK,
		.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
		.label = "quad-pipeline",
	};

	state.gfx.pip = sg_make_pipeline(&pipeline_desc);
}

void
gfx_done(void)
{
	sg_shutdown();
}

void
gfx_draw(void)
{
	const int canvas_width = sapp_width();
	const int canvas_height = sapp_height();

	screen_pattern_herringbone();
	screen_update();

	sg_begin_default_pass(&state.gfx.pass_action, canvas_width, canvas_height);
	sg_apply_pipeline(state.gfx.pip);
	sg_apply_bindings(&state.gfx.bind);
	// sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_demo3_vs_params, &SG_RANGE(vs_params));
	sg_draw(0, state.gfx.vertex_count, 1);
	sg_end_pass();
	sg_commit();
}
