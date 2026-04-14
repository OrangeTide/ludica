/*
 * args.c — Parse argc/argv into the config store.
 *
 * Scans all arguments into the key-value config store so both ludica
 * and the application can read them with lud_get_config().
 *
 * Convention:
 *   --key=value  or  -key=value   →  ("key", "value")
 *   --key        or  -key         →  ("key", "")
 *   --                            →  stop scanning
 *   anything else                 →  skip
 *
 * Each config entry remembers which argv index it came from.
 * lud_get_config() marks that index as read. At exit, any argv
 * flags that were never read are reported as warnings.
 */

#include "ludica_internal.h"
#include <stdlib.h>
#include <string.h>

static int saved_argc;
static char **saved_argv;
static char *readflag;	/* parallel to argv, 0 = unread */

void
lud__args_mark_read(int index)
{
	if (readflag && index > 0 && index < saved_argc)
		readflag[index] = 1;
}

void
lud__args_warn_unused(void)
{
	int i, n = 0;

	if (!readflag)
		return;

	for (i = 1; i < saved_argc; i++) {
		if (!readflag[i] && saved_argv[i] && saved_argv[i][0] == '-') {
			lud_err("warning: unused argument '%s'", saved_argv[i]);
			n++;
		}
	}
	if (n > 0)
		lud_err("warning: %d unused argument(s)", n);

	free(readflag);
	readflag = NULL;
}

static void
apply_ludica_config(lud_desc_t *desc)
{
	const char *val;

	if ((val = lud_get_config("auto-port")))
		lud__state.auto_port = atoi(val);
	if ((val = lud_get_config("auto-file")))
		lud__state.auto_file = val;
	if ((val = lud_get_config("capture-dir")))
		lud__state.capture_dir = val;
	if ((val = lud_get_config("width")))
		desc->width = atoi(val);
	if ((val = lud_get_config("height")))
		desc->height = atoi(val);
	if (lud_get_config("paused"))
		lud__state.paused = 1;
	if (lud_get_config("fixed-dt"))
		lud__state.fixed_dt = 1;
	if (lud_get_config("fullscreen"))
		desc->fullscreen = 1;
}

void
lud__parse_args(lud_desc_t *desc)
{
	int argc = desc->argc;
	char **argv = desc->argv;
	int i;

	if (argc <= 0 || !argv)
		return;

	saved_argc = argc;
	saved_argv = argv;
	readflag = calloc(argc, 1);

	/* argv[0] is the program name, always considered read */
	if (readflag)
		readflag[0] = 1;

	/* store program name so apps can use it for usage messages */
	lud__set_config_source("_program", argv[0], 0);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		const char *key, *eq;
		char keybuf[256];
		size_t keylen;

		if (!arg || arg[0] != '-')
			continue;

		/* "--" stops option scanning */
		if (arg[1] == '-' && arg[2] == '\0')
			break;

		/* skip leading dashes */
		key = arg + 1;
		if (*key == '-')
			key++;

		/* split at '=' if present */
		eq = strchr(key, '=');
		if (eq) {
			keylen = (size_t)(eq - key);
			if (keylen >= sizeof(keybuf))
				keylen = sizeof(keybuf) - 1;
			memcpy(keybuf, key, keylen);
			keybuf[keylen] = '\0';
			lud__set_config_source(keybuf, eq + 1, i);
		} else {
			lud__set_config_source(key, "", i);
		}
	}

	apply_ludica_config(desc);
}
