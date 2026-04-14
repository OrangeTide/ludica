/*
 * config.c — Platform-agnostic key-value configuration store.
 *
 * On desktop, populated from argv by args.c during lud_run().
 * On WASM, populated from JavaScript via exported lud_set_config().
 * Applications read config with lud_get_config().
 */

#include "ludica_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
	char *key;
	char *value;
	int argv_index;	/* >0 = index into argv this came from, 0 = not from argv */
} config_entry_t;

#define CONFIG_MAX 256

static config_entry_t store[CONFIG_MAX];
static int count;

const char *
lud_get_config(const char *key)
{
	int i;

	for (i = 0; i < count; i++) {
		if (strcmp(store[i].key, key) == 0) {
			if (store[i].argv_index > 0)
				lud__args_mark_read(store[i].argv_index);
			return store[i].value;
		}
	}

	return NULL;
}

int
lud_set_config(const char *key, const char *value)
{
	return lud__set_config_source(key, value, 0);
}

int
lud__set_config_source(const char *key, const char *value, int argv_index)
{
	int i;
	char *vdup;

	if (!value)
		value = "";

	/* update existing entry */
	for (i = 0; i < count; i++) {
		if (strcmp(store[i].key, key) == 0) {
			vdup = strdup(value);
			if (!vdup)
				return -1;
			free(store[i].value);
			store[i].value = vdup;
			if (argv_index > 0)
				store[i].argv_index = argv_index;
			return 0;
		}
	}

	/* append new entry */
	if (count >= CONFIG_MAX)
		return -1;

	store[count].key = strdup(key);
	store[count].value = strdup(value);
	if (!store[count].key || !store[count].value) {
		free(store[count].key);
		free(store[count].value);
		return -1;
	}
	store[count].argv_index = argv_index;
	count++;

	return 0;
}

void
lud__config_cleanup(void)
{
	int i;

	for (i = 0; i < count; i++) {
		free(store[i].key);
		free(store[i].value);
	}
	count = 0;
}
