/* debugmon.h - TCP debug monitor for lilpc */
#ifndef DEBUGMON_H
#define DEBUGMON_H

#include <stdint.h>
#include <stdbool.h>

struct lilpc;

#define DEBUGMON_MAX_BREAKPOINTS 16

typedef struct debugmon {
	int listen_fd;		/* listening socket (-1 if inactive) */
	int client_fd;		/* connected client (-1 if none) */
	int port;		/* TCP port number */

	/* input line buffer */
	char buf[4096];
	int buf_len;

	/* execution control */
	bool paused;		/* true = CPU is paused */
	bool step_one;		/* true = execute one instruction then pause */

	/* breakpoints */
	struct {
		uint16_t seg;
		uint16_t off;
		bool active;
	} bp[DEBUGMON_MAX_BREAKPOINTS];
	int bp_count;
} debugmon_t;

/* Initialize debug monitor on given TCP port. Returns 0 on success. */
int debugmon_init(debugmon_t *dm, int port);

/* Clean up sockets */
void debugmon_cleanup(debugmon_t *dm);

/* Poll for connections and commands. Called each frame.
 * Returns true if CPU should run this frame (not paused or stepping). */
bool debugmon_poll(debugmon_t *dm, struct lilpc *pc);

/* Check if current CS:IP hits a breakpoint */
bool debugmon_check_bp(debugmon_t *dm, uint16_t seg, uint16_t off);

/* Notify client that a breakpoint was hit or step completed */
void dm_notify_break(debugmon_t *dm, struct lilpc *pc);
void dm_notify_step(debugmon_t *dm, struct lilpc *pc);

#endif
