/* arena.c : bump/arena allocator implementation */
/* Made by a machine. PUBLIC DOMAIN (CC0-1.0) */

#include "ludica_arena.h"
#include "ludica.h"
#include <stdlib.h>

/* Alignment that satisfies every standard type. malloc already returns
 * memory aligned this way, so aligning each handout to it keeps every
 * allocation suitably aligned. */
#define ARENA_ALIGN (sizeof(max_align_t))

int
lud_arena_init(lud_arena_t *a, size_t size)
{
	a->buf = size ? malloc(size) : NULL;
	a->off = 0;
	a->cap = a->buf ? size : 0;
	return (size && !a->buf) ? LUD_ERR : LUD_OK;
}

void *
lud_arena_alloc(lud_arena_t *a, size_t size)
{
	size_t aligned;

	if (!a->buf || size == 0)
		return NULL;

	/* Round the current offset up to the alignment boundary. The
	 * `aligned < a->off` test rejects the (pathological) case where the
	 * rounding addition wraps; `size > a->cap - aligned` is the room
	 * check, written so it cannot itself underflow (aligned <= cap). */
	aligned = (a->off + (ARENA_ALIGN - 1)) & ~(ARENA_ALIGN - 1);
	if (aligned < a->off || aligned > a->cap || size > a->cap - aligned)
		return NULL;

	void *p = a->buf + aligned;
	a->off = aligned + size;
	return p;
}

void
lud_arena_reset(lud_arena_t *a)
{
	a->off = 0;
}

void
lud_arena_free(lud_arena_t *a)
{
	free(a->buf);
	a->buf = NULL;
	a->off = 0;
	a->cap = 0;
}
