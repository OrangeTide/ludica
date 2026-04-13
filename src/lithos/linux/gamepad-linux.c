#include "gamepad.h"
#include "gamepad-linux.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <linux/joystick.h>

static int gamepad_fd[GAMEPAD_MAX];

void
gamepad_close_one(int id)
{
	if (id < 0 || id >= GAMEPAD_MAX) {
		return;
	}
	if (gamepad_fd[id] != -1) {
		close(gamepad_fd[id]);
		gamepad_fd[id] = -1;
	}
	memset(&gamepad_state[id], 0, sizeof(*gamepad_state));
	gamepad_info[id].exists = false;
}

bool
gamepad_open_one(int id)
{
	char name[256];
	int fd;
	struct gamepad_state *gs;
	struct gamepad_info *info;
	char num_axis, num_button;
	int version;

	if (id < 0 || id >= GAMEPAD_MAX) {
	       return false;
	}

	/* open */
	gs = &gamepad_state[id];
	info = &gamepad_info[id];
	memset(gs, 0, sizeof(*gs));
	snprintf(name, sizeof(name), "/dev/input/js%u", id);
	gamepad_fd[id] = fd = open(name, O_RDWR | O_NONBLOCK);
	if (fd == -1) {
		if (errno != ENOENT) {
			perror(name);
		}
		return false;
	}

	/* check version support (we can't use 0.x drivers) */
	if ((ioctl(fd, JSIOCGVERSION, &version) < 0) || (version < 0x020100)) {
		gamepad_close_one(id);
		return false;
	}

	if (ioctl(fd, JSIOCGAXES, &num_axis) < 0) {
		perror(name);
		num_axis = 0;
	}
	if (ioctl(fd, JSIOCGBUTTONS, &num_button) < 0) {
		perror(name);
		num_button = 0;
	}
	if (ioctl(fd, JSIOCGNAME(sizeof(info->name)), info->name) < 0) {
		perror(name);
		info->name[0] = 0;
	}
	if (info->name[0] == 0) { /* if name is empty, fill in with something */
		snprintf(info->name, sizeof(info->name), "gamepad%u", id);
	}

	info->num_axis = num_axis;
	info->num_button = num_button;
	info->exists = true;

	fprintf(stderr, "gamepad%u: name=\"%s\" axes=%d buttons=%d\n",
			id, info->name, info->num_axis, info->num_button);

	return true;
}

/* process an event from one gamepad.
 * return - true if events were processed. false is no pending events. */
static bool
gamepad_process_one(int id)
{
	struct js_event ev;
	ssize_t len;
	struct gamepad_state *gs;
	unsigned i, evcount = 0;

	if (!gamepad_exists(id)) {
	       return false;
	}
	gs = &gamepad_state[id];

	/* linux joystick driver's FIFO is typically set to 64.
	 * use this small number to avoid reading too many updates at once
	 */
	while (evcount < 64) {
		len = read(gamepad_fd[id], &ev, sizeof(ev));
		if (len == 0 || (len == -1 && errno == EAGAIN)) {
			break;
		}
		if ((len < 0) || (len != sizeof(ev))) {
			perror(__func__);
			// TODO: maybe we should tag the entry as an error and black list events from it?
			gamepad_close_one(id);
			return true; /* we forcefully unplug it */
		}
		evcount++;
		switch (ev.type & ~JS_EVENT_INIT) {
		case JS_EVENT_BUTTON:
			i = ev.number / (sizeof(unsigned) * 8);
			if (i < GAMEPAD_BUTTON_MAX) {
				unsigned n = ev.number % (sizeof(unsigned) * 8);
				if (ev.value) {
					gs->button[i] |= 1UL << n;
				} else {
					gs->button[i] &= ~(1UL << n);
				}
			}
			break;
		case JS_EVENT_AXIS:
			i = ev.number;
			if (i < GAMEPAD_AXIS_MAX) {
				gs->axis[i] = ev.value / 32767.0f;
			}
			break;
		}
	}
	printf("evcount=%d\n", evcount);

	return evcount > 0;
}

/* return true if events have changed out state */
bool
gamepad_poll(void)
{
	unsigned i;
	bool result = false;

	for (i = 0; i < GAMEPAD_MAX; i++) {
		if (gamepad_info[i].exists) {
			result |= gamepad_process_one(i);
		}
	}

	return false;
}

/* wait gamepads for updates, with a timeout.
 * msec = 0 : no timeout
 * msec < 0 : wait forever
 * msec > 0 : milliseconds to wait for events. returns after processing events.
 * return true if events have changed out state. false if no state change. */
bool
gamepad_wait(int msec)
{
	fd_set rfds;
	int max_fd = -1, n;
	unsigned i;
	struct timeval tv;
	bool result;

	FD_ZERO(&rfds);
	for (i = 0; i < GAMEPAD_MAX; i++) {
		int fd = gamepad_fd[i];
		FD_SET(fd, &rfds);
		if (fd > max_fd)
			max_fd = fd;
	}

	// TODO: check /dev/input/ for changes to catch hotplug

	if (msec >= 0) {
		tv.tv_sec = msec / 1000l;
		tv.tv_usec = (msec % 1000l) * 1000l;
	}
	n = select(max_fd + 1, &rfds, NULL, NULL, msec < 0 ? NULL : &tv);
	if (n < 0) {
		perror("select()");
		return false;
	}

	result = false;
	for (i = 0; i < GAMEPAD_MAX; i++) {
		if (FD_ISSET(gamepad_fd[i], &rfds))
			result |= gamepad_process_one(i);
	}
	return result;
}

// TODO: calibration - do something with JSIOCGCORR
// TODO: implement a dead zone

/* Mappings:

Xbox360 - this is the default
Logitech Dual Action:
	# xboxdrv --evdev /dev/input/event* \
   --evdev-absmap ABS_X=x1,ABS_Y=y1,ABS_RZ=x2,ABS_Z=y2,ABS_HAT0X=dpad_x,ABS_HAT0Y=dpad_y \
   --axismap -Y1=Y1,-Y2=Y2 \
   --evdev-keymap BTN_TRIGGER=x,BTN_TOP=y,BTN_THUMB=a,BTN_THUMB2=b,BTN_BASE3=back,BTN_BASE4=start,BTN_BASE=lt,BTN_BASE2=rt,BTN_TOP2=lb,BTN_PINKIE=rb,BTN_BASE5=tl,BTN_BASE6=tr \
   --mimic-xpad --silent
Playstation 2 controller
	# xboxdrv --evdev /dev/input/event* \
   --evdev-absmap ABS_X=x1,ABS_Y=y1,ABS_RZ=x2,ABS_Z=y2,ABS_HAT0X=dpad_x,ABS_HAT0Y=dpad_y \
   --axismap -Y1=Y1,-Y2=Y2 \
   --evdev-keymap   BTN_TOP=x,BTN_TRIGGER=y,BTN_THUMB2=a,BTN_THUMB=b,BTN_BASE3=back,BTN_BASE4=start,BTN_BASE=lb,BTN_BASE2=rb,BTN_TOP2=lt,BTN_PINKIE=rt,BTN_BASE5=tl,BTN_BASE6=tr \
   --mimic-xpad --silent
Playstation 4 controller
	 # xboxdrv \
   --evdev /dev/input/by-id/usb-Sony_Computer_Entertainment_Wireless_Controller-event-joystick\
   --evdev-absmap ABS_X=x1,ABS_Y=y1                 \
   --evdev-absmap ABS_Z=x2,ABS_RZ=y2                \
   --evdev-absmap ABS_HAT0X=dpad_x,ABS_HAT0Y=dpad_y \
   --evdev-keymap BTN_A=x,BTN_B=a                   \
   --evdev-keymap BTN_C=b,BTN_X=y                   \
   --evdev-keymap BTN_Y=lb,BTN_Z=rb                 \
   --evdev-keymap BTN_TL=lt,BTN_TR=rt               \
   --evdev-keymap BTN_SELECT=tl,BTN_START=tr        \
   --evdev-keymap BTN_TL2=back,BTN_TR2=start        \
   --evdev-keymap BTN_MODE=guide                    \
   --axismap -y1=y1,-y2=y2                          \
   --mimic-xpad                                     \
   --silent


*/
