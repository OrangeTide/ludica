PROJECT_CFLAGS := -Wall -W
PROJECT_CXXFLAGS := -Wall -W

SUBDIRS = thirdparty ludica imgui demo01_retrocrt demo02_multiscroll demo03_text_dialogs ansiview hero

# --- Shader-to-C generation ---
# Annotated .glsl files (@common/@vs/@fs/@end) are converted to C files
# containing const char[] arrays with the shader source.  The generated
# .c files live in $(BUILDDIR) and are listed in each target's
# _GENERATED_SRCS.  Other languages can load .glsl files at runtime.

$(BUILDDIR)/src/ludica/shaders/%.c : src/ludica/shaders/%.glsl tools/glsl2h
	tools/glsl2h $< > $@
$(BUILDDIR)/src/hero/shaders/%.c : src/hero/shaders/%.glsl tools/glsl2h
	tools/glsl2h $< > $@
