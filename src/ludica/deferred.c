/* deferred.c : end-of-frame deletion queue for GPU resources */
/* Made by a machine. PUBLIC DOMAIN (CC0-1.0) */

#include "ludica_internal.h"
#include "ludica_gfx.h"
#include <stdlib.h>

/* When a resource is freed mid-frame while a draw still references it, an
 * immediate delete would invalidate the handle the draw needs. These calls
 * queue the delete instead; lud__deferred_flush runs it after the frame
 * callback returns, once all of the frame's draws have been issued. */

enum { KIND_MESH, KIND_TEXTURE };

struct pending {
	int kind;
	unsigned id;
};

static struct pending *queue;
static int count;
static int cap;

static void
push(int kind, unsigned id)
{
	if (id == 0)
		return;
	if (count == cap) {
		int ncap = cap ? cap * 2 : 16;
		struct pending *q = realloc(queue, (size_t)ncap * sizeof *q);
		if (!q) {
			/* Out of memory: fall back to deleting now rather than
			 * leaking the resource. Safe at end-of-frame callers. */
			lud_err("deferred-destroy queue grow failed; deleting now");
			if (kind == KIND_MESH)
				lud_destroy_mesh((lud_mesh_t){ id });
			else
				lud_destroy_texture((lud_texture_t){ id });
			return;
		}
		queue = q;
		cap = ncap;
	}
	queue[count].kind = kind;
	queue[count].id = id;
	count++;
}

void
lud_destroy_mesh_deferred(lud_mesh_t mesh)
{
	push(KIND_MESH, mesh.id);
}

void
lud_destroy_texture_deferred(lud_texture_t tex)
{
	push(KIND_TEXTURE, tex.id);
}

/* Run every queued deletion. Called once per frame after the frame
 * callback, and again at shutdown. */
void
lud__deferred_flush(void)
{
	int i;

	for (i = 0; i < count; i++) {
		if (queue[i].kind == KIND_MESH)
			lud_destroy_mesh((lud_mesh_t){ queue[i].id });
		else
			lud_destroy_texture((lud_texture_t){ queue[i].id });
	}
	count = 0;
}

void
lud__deferred_cleanup(void)
{
	lud__deferred_flush();
	free(queue);
	queue = NULL;
	cap = 0;
}
