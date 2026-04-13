#ifndef GAMEPAD_H
#define GAMEPAD_H
#include <stdbool.h>
#include <math.h>

#define GAMEPAD_MAX 4
#define GAMEPAD_BUTTON_MAX 64
#define GAMEPAD_AXIS_MAX 8

struct gamepad_state {
	/** dynamic state **/
	unsigned button[(GAMEPAD_BUTTON_MAX + (sizeof(unsigned) * 8 - 1))
		/ (sizeof(unsigned) * 8)]; /* bitmap of buttons */
	float axis[GAMEPAD_AXIS_MAX];

};

struct gamepad_info {
	/** static infomation **/
	unsigned char exists;
	unsigned char num_axis;
	unsigned char num_button;
	char name[60];
};

extern struct gamepad_state gamepad_state[GAMEPAD_MAX];
extern struct gamepad_info gamepad_info[GAMEPAD_MAX];

bool gamepad_init(void);
void gamepad_done(void);
bool gamepad_poll(void);
bool gamepad_wait(int msec);
void gamepad_dump(void);

static inline bool
gamepad_exists(int id)
{
	return (id < 0 || id >= GAMEPAD_MAX) ? false : gamepad_info[id].exists;
}

/* looks at the state of a single button. */
static inline bool
gamepad_button(int id, int button)
{
	unsigned i, n;

	if (!gamepad_exists(id)) {
		return false;
	}
	i = button / (sizeof(unsigned) * 8);
	n = button % (sizeof(unsigned) * 8);
	return gamepad_state[id].button[i] & (1UL << n) ? true : false;
}

/* query the state of an axis. */
static inline float
gamepad_axis(int id, int axis)
{
	if (!gamepad_exists(id)) {
		return NAN;
	}
	return gamepad_state[id].axis[axis];
}
#endif /* GAMEPAD_H */
