/*
 * args.c — Command-line argument parsing for ludica.
 *
 * Scans argc/argv from lud_desc_t for ludica's own --flags, applies
 * them, and removes them from argv (via memmove) so the application
 * only sees its own arguments. Apps can then use lud_getopt() on the
 * remaining argv.
 */

#include "ludica_internal.h"
#include <string.h>
#include <stdlib.h>

/* Table of known --long-option flags.
 * has_arg: 0 = no argument, 1 = requires next argv as argument */
static const struct {
	const char *name;
	int has_arg;
} known_flags[] = {
	{ "--auto-port",   1 },
	{ "--auto-file",   1 },
	{ "--capture-dir", 1 },
	{ "--width",       1 },
	{ "--height",      1 },
	{ "--paused",      0 },
	{ "--fixed-dt",    0 },
	{ "--fullscreen",  0 },
};

#define NFLAGS (sizeof(known_flags) / sizeof(known_flags[0]))

/* Apply a recognized flag. Called with the flag name and its argument
 * (NULL if the flag takes no argument). */
static void
apply_flag(lud_desc_t *desc, const char *flag, const char *arg)
{
	if (strcmp(flag, "--auto-port") == 0)
		lud__state.auto_port = atoi(arg);
	else if (strcmp(flag, "--auto-file") == 0)
		lud__state.auto_file = arg;
	else if (strcmp(flag, "--capture-dir") == 0)
		lud__state.capture_dir = arg;
	else if (strcmp(flag, "--width") == 0)
		desc->width = atoi(arg);
	else if (strcmp(flag, "--height") == 0)
		desc->height = atoi(arg);
	else if (strcmp(flag, "--paused") == 0)
		lud__state.paused = 1;
	else if (strcmp(flag, "--fixed-dt") == 0)
		lud__state.fixed_dt = 1;
	else if (strcmp(flag, "--fullscreen") == 0)
		desc->fullscreen = 1;
}

void
lud__parse_args(lud_desc_t *desc)
{
	int argc = desc->argc;
	char **argv = desc->argv;
	int i, j, k;

	if (argc <= 0 || !argv)
		return;

	i = 1; /* skip argv[0] (program name) */
	while (i < argc) {
		const char *arg = argv[i];

		if (!arg || arg[0] != '-' || arg[1] != '-') {
			i++;
			continue;
		}

		/* look up in known flags table */
		for (k = 0; k < (int)NFLAGS; k++) {
			if (strcmp(arg, known_flags[k].name) == 0)
				break;
		}

		if (k == (int)NFLAGS) {
			/* not a ludica flag — leave it for the app */
			i++;
			continue;
		}

		/* how many argv entries does this flag consume? */
		int consume = 1;
		const char *flag_arg = NULL;
		if (known_flags[k].has_arg) {
			if (i + 1 < argc) {
				flag_arg = argv[i + 1];
				consume = 2;
			} else {
				/* missing argument — skip, don't crash */
				i++;
				continue;
			}
		}

		apply_flag(desc, arg, flag_arg);

		/* remove consumed entries from argv */
		argc -= consume;
		for (j = i; j < argc; j++)
			argv[j] = argv[j + consume];
		argv[argc] = NULL;
		/* don't increment i — next element slid into position */
	}

	desc->argc = argc;
}
