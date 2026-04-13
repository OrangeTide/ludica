/*
 * args.c — Command-line argument parsing for ludica.
 *
 * Scans argc/argv from lud_desc_t for --flags and applies overrides
 * to the descriptor and internal state. Unknown flags are silently
 * skipped so apps can handle their own arguments.
 */

#include "ludica_internal.h"
#include <string.h>
#include <stdlib.h>

void
lud__parse_args(lud_desc_t *desc)
{
	int i;
	int argc = desc->argc;
	const char *const *argv = desc->argv;

	if (argc <= 0 || !argv)
		return;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!arg || arg[0] != '-')
			continue;

		if (strcmp(arg, "--auto-port") == 0 && i + 1 < argc) {
			lud__state.auto_port = atoi(argv[++i]);
		} else if (strcmp(arg, "--auto-file") == 0 && i + 1 < argc) {
			lud__state.auto_file = argv[++i];
		} else if (strcmp(arg, "--capture-dir") == 0 && i + 1 < argc) {
			lud__state.capture_dir = argv[++i];
		} else if (strcmp(arg, "--width") == 0 && i + 1 < argc) {
			desc->width = atoi(argv[++i]);
		} else if (strcmp(arg, "--height") == 0 && i + 1 < argc) {
			desc->height = atoi(argv[++i]);
		} else if (strcmp(arg, "--paused") == 0) {
			lud__state.paused = 1;
		} else if (strcmp(arg, "--fixed-dt") == 0) {
			lud__state.fixed_dt = 1;
		} else if (strcmp(arg, "--fullscreen") == 0) {
			desc->fullscreen = 1;
		}
	}
}
