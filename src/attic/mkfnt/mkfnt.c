/* mkfnt.c : makes a binary bitmap font into C source. - public domain */
#include <stdio.h>
#include <stdlib.h>

static const unsigned font_w = 8, font_h = 8;
static unsigned char *font;
static int total_glyphs;
static int font_base;

static size_t
glyphbytes(void)
{
	return font_h * ((font_w + 7) / 8);
}

static void
header(void)
{
	printf("static const unsigned font_w = %u, font_h = %u;\n", font_w, font_h);
	printf("static const unsigned char font_base = %#x, font_glyphs = %d;\n",
			font_base, total_glyphs);
	printf("static const unsigned char font[%u * %u] = {\n",
		       	total_glyphs, (int)glyphbytes());
}

static void
middle(unsigned char start, unsigned char end)
{
	const unsigned char *c = font;
	unsigned i, row, col;

	for (i = start; i <= end; i++) {
		if (i >= ' ' && i <= '~')
			printf("\t/* '%c'  */ ", i);
		else
			printf("\t/* 0x%02X */ ", i);
		for (row = 0; row < font_h; row++) {
			for (col = 0; col < font_w; col += 8) {
				printf("%u, ", *c);
				c++;
			}
		}
		putchar('\n');
	}
}

static void
footer(void)
{
	puts("};");
}

static int
load(int argc, char **argv)
{
	const char *fn = argc > 1 ? argv[1] : "font.fnt";
	FILE *f;

	f = fopen(fn, "rb");
	if (!f)
		return perror(fn), -1;
	total_glyphs = fread(font, glyphbytes(), 256, f);
	fclose(f);

	return 0;
}

int
main(int argc, char **argv)
{
	font = calloc(256, glyphbytes());
	if (!font)
		return perror("malloc()"), 1;

	if (load(argc, argv))
		return free(font), 1;

	if (!total_glyphs)
		return fprintf(stderr, "short file\n"), 1;

	if (total_glyphs < 128) {
		font_base = ' ';
	} else {
		font_base = 0;
	}

	header();
	middle(font_base, font_base + total_glyphs - 1);
	footer();

	free(font);

	return 0;
}
