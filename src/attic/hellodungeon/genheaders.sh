#!/bin/sh
# For more information, see:
# https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md
set -ex
MODULE=hello
for SHADER in *.glsl ; do
  sokol-shdc --input "$SHADER" --output "$SHADER".gl.h --slang glsl330 --genver 1 --errfmt gcc --format sokol --bytecode --module "$MODULE" --reflection
  sokol-shdc --input "$SHADER" --output "$SHADER".android.h --slang glsl300es --genver 1 --errfmt gcc --format sokol --bytecode --module "$MODULE" --reflection
  sokol-shdc --input "$SHADER" --output "$SHADER".webgl.h --slang glsl300es:glsl100 --genver 1 --errfmt gcc --format sokol --bytecode --module "$MODULE" --reflection
done
