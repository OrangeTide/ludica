/* arena_test.c : exercise the lud_arena bump allocator */
/* Made by a machine. PUBLIC DOMAIN (CC0-1.0) */

#include "ludica_arena.h"
#include "ludica.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

static int checks;
static int failures;

static void
expect(const char *what, int cond)
{
	checks++;
	if (cond) {
		printf("ok   %s\n", what);
	} else {
		failures++;
		printf("FAIL %s\n", what);
	}
}

int
main(void)
{
	lud_arena_t a;
	void *p, *q, *base;
	const size_t align = _Alignof(max_align_t);

	/* init */
	expect("init 1024 succeeds", lud_arena_init(&a, 1024) == LUD_OK);
	expect("init sets cap", a.cap == 1024);
	expect("init starts empty", a.off == 0);
	expect("init allocates a buffer", a.buf != NULL);

	/* basic allocation lands inside the buffer */
	p = lud_arena_alloc(&a, 100);
	expect("alloc returns non-NULL", p != NULL);
	expect("alloc is inside the buffer",
	       (unsigned char *)p >= a.buf && (unsigned char *)p < a.buf + a.cap);
	expect("alloc advances off", a.off >= 100);
	base = p;

	/* the region is writable and does not stomp the arena bookkeeping */
	memset(p, 0xAB, 100);
	expect("written bytes survive", ((unsigned char *)p)[0] == 0xAB &&
	       ((unsigned char *)p)[99] == 0xAB);

	/* alignment: a 1-byte alloc must not misalign the next handout */
	(void)lud_arena_alloc(&a, 1);
	q = lud_arena_alloc(&a, 64);
	expect("alloc is aligned for any type", ((uintptr_t)q % align) == 0);
	expect("distinct allocs do not overlap", q != p && q > p);

	/* zero-size alloc is rejected */
	expect("zero-size alloc returns NULL", lud_arena_alloc(&a, 0) == NULL);

	/* exhaustion: a request larger than the whole buffer fails and leaves
	 * the arena usable */
	{
		size_t off_before = a.off;
		expect("oversized alloc returns NULL",
		       lud_arena_alloc(&a, 4096) == NULL);
		expect("failed alloc does not advance off", a.off == off_before);
		expect("arena still serves a fitting request",
		       lud_arena_alloc(&a, 8) != NULL);
	}

	/* reset reclaims everything and reuses the same backing memory */
	lud_arena_reset(&a);
	expect("reset empties the arena", a.off == 0);
	p = lud_arena_alloc(&a, 100);
	expect("alloc after reset reuses the base address", p == base);

	/* free leaves the arena safe to touch again */
	lud_arena_free(&a);
	expect("free clears the buffer", a.buf == NULL && a.cap == 0);
	expect("alloc on a freed arena returns NULL",
	       lud_arena_alloc(&a, 16) == NULL);
	lud_arena_free(&a);  /* double free must be safe */
	expect("double free is safe", a.buf == NULL);

	/* a zero-size init yields an arena that allocates nothing */
	expect("init 0 succeeds", lud_arena_init(&a, 0) == LUD_OK);
	expect("zero-cap arena allocates nothing",
	       lud_arena_alloc(&a, 1) == NULL);
	lud_arena_free(&a);

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
