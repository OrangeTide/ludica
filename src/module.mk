PROJECT_CFLAGS := -Wall -W
PROJECT_CXXFLAGS := -Wall -W

SUBDIRS = thirdparty ludica imgui ../samples ../tools

# --- Shader-to-C generation ---
# Annotated .glsl files (@common/@vs/@fs/@end) are converted to C files
# containing const char[] arrays with the shader source.  The generated
# .c files live in $(BUILDDIR) and are listed in each target's
# _GENERATED_SRCS.  Other languages can load .glsl files at runtime.

$(BUILDDIR)/%.c : %.glsl tools/glsl2h
	tools/glsl2h $< > $@
