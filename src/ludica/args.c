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
 */

#include "ludica_internal.h"
#include <stdlib.h>
#include <string.h>

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

	/* store program name so apps can use it for usage messages */
	lud_set_config("_program", argv[0]);

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
			lud_set_config(keybuf, eq + 1);
		} else {
			lud_set_config(key, "");
		}
	}

	apply_ludica_config(desc);
}
