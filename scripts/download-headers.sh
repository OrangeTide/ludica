#!/bin/sh
set -e

# POSIX shell trick for making an array
# OpenGL ES 2.0, OpenGL ES 3.0, 3.1, and 3.2, Khronos EGL
set -- \
"https://registry.khronos.org/OpenGL/api/GLES2/gl2.h" \
"https://registry.khronos.org/OpenGL/api/GLES2/gl2ext.h" \
"https://registry.khronos.org/OpenGL/api/GLES2/gl2platform.h" \
"https://registry.khronos.org/OpenGL/api/GLES3/gl32.h" \
"https://registry.khronos.org/OpenGL/api/GLES3/gl31.h" \
"https://registry.khronos.org/OpenGL/api/GLES3/gl3.h" \
"https://registry.khronos.org/OpenGL/api/GLES3/gl3platform.h" \
"https://registry.khronos.org/EGL/api/EGL/egl.h" \
"https://registry.khronos.org/EGL/api/EGL/eglext.h" \
"https://registry.khronos.org/EGL/api/EGL/eglplatform.h" \
"https://registry.khronos.org/EGL/api/KHR/khrplatform.h"

INCDIR="src/ludica/include"

for url in "$@" ; do
  B="${url##*://*/api/}"
  if [ ! -e "${INCDIR}/$B" ]; then
    echo "Downloading $url ..."
    curl -sS -L --create-dirs --output "${INCDIR}/$B" "$url"
  else
    echo "[SKIP] Downloading $url ..."
  fi
done

cat <<EOF | sha1sum -c
108a22ccb13ea471940804584e01fbefa78a4872  ${INCDIR}/EGL/eglext.h
30b8964110af967b8ca07ef4530f56a88d849c21  ${INCDIR}/EGL/egl.h
60963b845b8d7570520c853f5e0a36a645fd3cbc  ${INCDIR}/EGL/eglplatform.h
0e8f5f850f65c859faf561f8495a83ecd13b7030  ${INCDIR}/GLES2/gl2ext.h
8243af14859460eb33bfc933687dc8388fab0d3b  ${INCDIR}/GLES2/gl2.h
39b2372c3d254addee407682f44760ca22116c7e  ${INCDIR}/GLES2/gl2platform.h
eb90411ed714acc684c225e1ae7d3105e6a0a9e3  ${INCDIR}/GLES3/gl31.h
44cc0f878a3eef929fbc576ff7b9e614e005a351  ${INCDIR}/GLES3/gl32.h
00a12f93da92c9d0d1dfd0842c7fcfc070773404  ${INCDIR}/GLES3/gl3.h
a032fbbd07a90fd378a154834d3dc6ce93ee198d  ${INCDIR}/GLES3/gl3platform.h
9818ea12cfb96c60bdbb5103d8cf7754dcce34f4  ${INCDIR}/KHR/khrplatform.h
EOF
