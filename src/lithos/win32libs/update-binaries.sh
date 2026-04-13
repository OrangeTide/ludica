#!/bin/sh
#set -x
set -e

URL="https://github.com/RobRich999/Chromium_Clang/releases/download/v114.0.5690.0-r1125063-win64-avx/chrome.zip"
SHA1=5725aec68a9805f4455cb8e93cda5df230ccb648
FILENAME="$(basename $URL)"

if [ ! -f "$FILENAME" ] ; then
  curl -L -O $URL
fi
if echo "$SHA1 $FILENAME" | sha1sum -s -c ; then
  echo "$FILENAME OK"
else
  echo "$FILENAME Error"
  exit 1
fi

# hack to do POSIX shell arrays
set -- "chrome-win32/libGLESv2.dll" "chrome-win32/libEGL.dll" "chrome-win32/vk_swiftshader.dll" "chrome-win32/vk_swiftshader_icd.json" "chrome-win32/d3dcompiler_47.dll"

unzip -joq "$FILENAME" "$@"

cat <<EOF | sha1sum -c
83097400436f111c13ee34740e66b3de0542914b  d3dcompiler_47.dll
603510b1a3dc93715484caabc2aff056819e10b9  libEGL.dll
0cca84246de48bc7d059a3ce1797afef148232b1  libGLESv2.dll
030050a72784b730daff31ec348db50ecc0f178a  vk_swiftshader.dll
9c06735c31cec00600fd763a92f8112d085bd12a  vk_swiftshader_icd.json
EOF
