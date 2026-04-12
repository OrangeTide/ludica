/* graphics.c */
#include "state.h"
#include "graphics.h"
#include "HandmadeMath.h"
#if defined(__ANDROID__)
#include "basic.glsl.android.h"
#elif defined(__EMSCRIPTEN__)
#include "basic.glsl.webgl.h"
#else
#include "basic.glsl.gl.h"
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

	sg_image_desc basic_desc = {
		.width = 256,
		.height = 1,
		// TODO: .data.subimage[0][0] = SG_RANGE(state.screen.basic),
		.label = "basic-tex",
	};
	sg_image basic = sg_make_image(&basic_desc);
	state.gfx.bind.fs_images[SLOT_hello_u_tex] = basic;

	sg_shader shd = sg_make_shader(hello_basic_shader_desc(sg_query_backend()));

	sg_pipeline_desc pipeline_desc = {
		.shader = shd,
		.layout = {
			.attrs = {
				[ATTR_hello_vs_position].format = SG_VERTEXFORMAT_FLOAT3,
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

	sg_begin_default_pass(&state.gfx.pass_action, canvas_width, canvas_height);
	sg_apply_pipeline(state.gfx.pip);
	sg_apply_bindings(&state.gfx.bind);
	// sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_hello_vs_params, &SG_RANGE(vs_params));
	sg_draw(0, state.gfx.vertex_count, 1);
	sg_end_pass();
	sg_commit();
}
