#!/bin/sh
# gen-pages.sh : Generate GitHub Pages site with WASM demos
#
# Usage: tools/gen-pages.sh [outdir]
#
# Expects WASM build output in _out/wasm32-unknown-emscripten/bin/
# and screenshots in screenshots/

set -e

OUTDIR="${1:-_site}"
WASMBIN="_out/wasm32-unknown-emscripten/bin"
SSDIR="screenshots"
VERSION="$(cat VERSION 2>/dev/null || echo unknown)"

mkdir -p "$OUTDIR/screenshots" "$OUTDIR/assets"

# --- Sample metadata (name|title|description) ---
SAMPLES="
hero|hero|Portal-based 3D engine with normal-mapped PBR surfaces, multiple rooms connected by portals, and dynamic lighting. Uses GLES3 for sRGB textures and advanced shading.
demo01_retrocrt|demo01_retrocrt|Indexed-color framebuffer with CRT post-processing. Renders to a 320x200 palette buffer, then applies scanlines, curvature, and phosphor glow via a fullscreen shader.
demo02_multiscroll|demo02_multiscroll|Multi-layer parallax scrolling with sprite batching. Tile-based backgrounds scroll at different speeds to create depth, with animated sprites composited on top.
demo03_text_dialogs|demo03_text_dialogs|Bitmap font rendering and dialog box UI. Demonstrates text layout, word wrapping, and a typewriter-style reveal effect inside styled dialog frames.
demo04_sprites|demo04_sprites|Sprite rendering with animation and simple physics. A character walks and jumps on a tiled platform, demonstrating sprite sheets and frame-based animation.
demo05_audio|demo05_audio|Multi-channel audio mixer with capture support. Plays layered sounds with volume control, panning, and real-time waveform visualization.
tridrop|tridrop|Triangle block puzzle game. Drag and drop triangle pieces onto a hexagonal grid, clearing lines to score points.
ansiview|ansiview|ANSI art viewer that renders .ANS files using a CP437 bitmap font. Supports 16-color palette and blinking attributes.
lilpc|lilpc|Tiny 286 XT PC emulator with CGA display. Boots a custom BIOS and runs a demo disk image, rendering CGA text and graphics modes.
"

# --- Copy WASM build artifacts ---
for name in $(echo "$SAMPLES" | awk -F'|' 'NF{print $1}'); do
	for ext in html js wasm data; do
		src="$WASMBIN/$name.$ext"
		if [ -f "$src" ]; then
			cp "$src" "$OUTDIR/"
		fi
	done
done

# --- Copy screenshots ---
for img in "$SSDIR"/*.jpg "$SSDIR"/*.png; do
	[ -f "$img" ] && cp "$img" "$OUTDIR/screenshots/"
done

# --- Copy assets for hero (fetched over HTTP, not bundled in .data) ---
if [ -d "assets" ]; then
	cp -r assets "$OUTDIR/"
fi

# --- Copy lilpc disk images (fetched over HTTP, not bundled in .data) ---
if [ -d "samples/lilpc/disk" ]; then
	mkdir -p "$OUTDIR/disk"
	cp samples/lilpc/disk/*.json samples/lilpc/disk/*.img "$OUTDIR/disk/" 2>/dev/null || true
fi

# --- Build manual if pandoc is available ---
if command -v pandoc >/dev/null 2>&1 && [ -f doc/manual/manual.md ]; then
	make -C doc/manual
fi

# --- Find screenshot for a sample ---
find_screenshot() {
	for ext in jpg png; do
		if [ -f "$OUTDIR/screenshots/$1.$ext" ]; then
			echo "screenshots/$1.$ext"
			return
		fi
	done
	echo ""
}

# --- Check if WASM build exists for a sample ---
has_wasm() {
	[ -f "$OUTDIR/$1.html" ]
}

# --- Generate index.html ---
cat > "$OUTDIR/index.html" << 'HTMLHEAD'
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ludica -- game development experiments</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
html {
  background: #000;
  color: #aaa;
  font: 16px/1.6 "Courier New", Courier, monospace;
}
body {
  max-width: 960px;
  margin: 0 auto;
  padding: 24px 16px 48px;
}
h1 {
  color: #ccc;
  font-size: 28px;
  font-weight: normal;
  margin-bottom: 4px;
  letter-spacing: 2px;
}
.subtitle {
  color: #666;
  font-size: 14px;
  margin-bottom: 32px;
  border-bottom: 1px solid #333;
  padding-bottom: 16px;
}
.sample {
  margin-bottom: 32px;
  overflow: hidden;
  border-bottom: 1px solid #222;
  padding-bottom: 24px;
}
.sample:last-of-type {
  border-bottom: none;
}
.thumb {
  float: left;
  margin: 4px 20px 12px 0;
  width: 320px;
  height: 200px;
  display: block;
  border: 1px solid #333;
  image-rendering: auto;
}
.thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}
.thumb-empty {
  float: left;
  margin: 4px 20px 12px 0;
  width: 320px;
  height: 200px;
  background: #1a1a1a;
  border: 1px solid #333;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #444;
  font-size: 14px;
}
.sample h2 {
  color: #ddd;
  font-size: 18px;
  font-weight: normal;
  margin-bottom: 4px;
}
.sample h2 a {
  color: #ddd;
  text-decoration: none;
}
.sample h2 a:hover {
  color: #fff;
  text-decoration: underline;
}
.sample h2 .nolink {
  color: #666;
}
.sample p {
  color: #888;
  font-size: 14px;
  line-height: 1.7;
}
.links {
  margin-top: 8px;
  font-size: 13px;
}
.links a {
  color: #5af;
  text-decoration: none;
  margin-right: 12px;
}
.links a:hover {
  text-decoration: underline;
}
footer {
  margin-top: 48px;
  padding-top: 16px;
  border-top: 1px solid #333;
  text-align: center;
  color: #555;
  font-size: 13px;
}
footer a {
  color: #5af;
  text-decoration: none;
}
footer a:hover {
  text-decoration: underline;
}
</style>
</head>
<body>
<h1>ludica</h1>
HTMLHEAD

# Write the subtitle with version
cat >> "$OUTDIR/index.html" << EOF
<p class="subtitle">game development experiments &mdash; v${VERSION}</p>
EOF

# --- Write each sample entry ---
echo "$SAMPLES" | while IFS='|' read -r name title desc; do
	[ -z "$name" ] && continue

	ss=$(find_screenshot "$name")

	echo '<div class="sample">' >> "$OUTDIR/index.html"

	# Thumbnail (linked if WASM available)
	if [ -n "$ss" ]; then
		if has_wasm "$name"; then
			printf '<a class="thumb" href="%s.html"><img src="%s" alt="%s"></a>\n' \
				"$name" "$ss" "$title" >> "$OUTDIR/index.html"
		else
			printf '<div class="thumb"><img src="%s" alt="%s"></div>\n' \
				"$ss" "$title" >> "$OUTDIR/index.html"
		fi
	else
		if has_wasm "$name"; then
			printf '<a class="thumb-empty" href="%s.html">no screenshot</a>\n' \
				"$name" >> "$OUTDIR/index.html"
		else
			printf '<div class="thumb-empty">no screenshot</div>\n' >> "$OUTDIR/index.html"
		fi
	fi

	# Title (linked if WASM available)
	if has_wasm "$name"; then
		printf '<h2><a href="%s.html">%s</a></h2>\n' "$name" "$title" >> "$OUTDIR/index.html"
	else
		printf '<h2><span class="nolink">%s</span></h2>\n' "$title" >> "$OUTDIR/index.html"
	fi

	# Description
	printf '<p>%s</p>\n' "$desc" >> "$OUTDIR/index.html"

	# Links row
	echo '<div class="links">' >> "$OUTDIR/index.html"
	if has_wasm "$name"; then
		printf '<a href="%s.html">&#9654; Play in browser</a>' "$name" >> "$OUTDIR/index.html"
	fi
	echo '</div>' >> "$OUTDIR/index.html"
	echo '</div>' >> "$OUTDIR/index.html"
done

# --- Footer ---
cat >> "$OUTDIR/index.html" << 'HTMLFOOT'
<footer>
<a href="https://github.com/OrangeTide/ludica">ludica on GitHub</a>
&mdash; 0BSD License
</footer>
</body>
</html>
HTMLFOOT

echo "Site generated in $OUTDIR/"
echo "  samples: $(ls "$OUTDIR"/*.html 2>/dev/null | grep -cv index.html) WASM demos"
echo "  screenshots: $(ls "$OUTDIR"/screenshots/ 2>/dev/null | wc -l) images"
