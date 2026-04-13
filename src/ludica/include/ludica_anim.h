#ifndef LUDICA_ANIM_H_
#define LUDICA_ANIM_H_

/* Frame-based sprite animation player.
 *
 * Usage:
 *   lud_anim_t anim;
 *   lud_anim_init(&anim, 0, 5, 8.0f, 1);  // frames 0-5, 8 FPS, loop
 *   ...
 *   lud_anim_update(&anim, dt);
 *   int frame = lud_anim_frame(&anim);
 *   // map frame to spritesheet grid position
 */

typedef struct {
	int start;          /* first frame index */
	int end;            /* last frame index (inclusive) */
	float fps;          /* playback speed */
	int looping;        /* non-zero = loop, zero = one-shot */
	int frame;          /* current frame index */
	float timer;        /* accumulator */
	int finished;       /* non-zero if one-shot completed */
} lud_anim_t;

/* Initialize or switch to a new animation. Resets frame and timer. */
void lud_anim_init(lud_anim_t *anim, int start, int end,
                   float fps, int looping);

/* Switch animation only if different from current. Returns 1 if switched. */
int lud_anim_play(lud_anim_t *anim, int start, int end,
                  float fps, int looping);

/* Advance the animation by dt seconds. */
void lud_anim_update(lud_anim_t *anim, float dt);

/* Get the current frame index. */
int lud_anim_frame(const lud_anim_t *anim);

/* Returns non-zero if a one-shot animation has completed. */
int lud_anim_finished(const lud_anim_t *anim);

#endif /* LUDICA_ANIM_H_ */
