/* ludica_arena.h : bump/arena allocator for fast, bulk-freeable scratch */
/* Made by a machine. PUBLIC DOMAIN (CC0-1.0) */

#ifndef LUDICA_ARENA_H_
#define LUDICA_ARENA_H_

#include <stddef.h>

/* A linear (bump) allocator over a single fixed buffer. Allocation is a
 * pointer bump, so there is no per-allocation free: reset the whole arena
 * at once (lud_arena_reset) or destroy it (lud_arena_free). Useful for job
 * scratch data, per-frame temporaries, and procedural-generation buffers
 * where everything is discarded together.
 *
 * The struct is public so callers can read off/cap directly (bytes used is
 * `off`, bytes free is `cap - off`); use the API to allocate. */
typedef struct lud_arena {
	unsigned char *buf;     /* backing buffer, or NULL if init failed */
	size_t off;             /* bytes handed out so far */
	size_t cap;             /* backing buffer size in bytes */
} lud_arena_t;

/* Allocate a backing buffer of `size` bytes. On failure the arena is left
 * empty (buf == NULL, cap == 0) and every lud_arena_alloc returns NULL.
 * Returns LUD_OK on success, LUD_ERR on allocation failure. */
int lud_arena_init(lud_arena_t *a, size_t size);

/* Hand out `size` bytes from the arena, aligned for any standard type.
 * Returns NULL if the arena lacks room (the arena does not grow) or if
 * `size` is zero. The memory is uninitialized. */
void *lud_arena_alloc(lud_arena_t *a, size_t size);

/* Reclaim every allocation at once; the backing buffer is kept so the
 * arena can be filled again. Pointers from earlier allocs become stale. */
void lud_arena_reset(lud_arena_t *a);

/* Release the backing buffer and zero the arena. Safe to call on a
 * zero-initialized or already-freed arena. */
void lud_arena_free(lud_arena_t *a);

#endif /* LUDICA_ARENA_H_ */
