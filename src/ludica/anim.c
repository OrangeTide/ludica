#include "ludica_anim.h"

void
lud_anim_init(lud_anim_t *anim, int start, int end, float fps, int looping)
{
	anim->start = start;
	anim->end = end;
	anim->fps = fps;
	anim->looping = looping;
	anim->frame = start;
	anim->timer = 0.0f;
	anim->finished = 0;
}

int
lud_anim_play(lud_anim_t *anim, int start, int end, float fps, int looping)
{
	if (anim->start == start && anim->end == end)
		return 0;
	lud_anim_init(anim, start, end, fps, looping);
	return 1;
}

void
lud_anim_update(lud_anim_t *anim, float dt)
{
	float spf;

	if (anim->finished || anim->fps <= 0.0f)
		return;

	spf = 1.0f / anim->fps;
	anim->timer += dt;

	while (anim->timer >= spf) {
		anim->timer -= spf;
		anim->frame++;
		if (anim->frame > anim->end) {
			if (anim->looping) {
				anim->frame = anim->start;
			} else {
				anim->frame = anim->end;
				anim->finished = 1;
				anim->timer = 0.0f;
				return;
			}
		}
	}
}

int
lud_anim_frame(const lud_anim_t *anim)
{
	return anim->frame;
}

int
lud_anim_finished(const lud_anim_t *anim)
{
	return anim->finished;
}
