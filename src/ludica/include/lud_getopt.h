/* lud_getopt.h : portable command-line argument processing - public domain */
#ifndef LUD_GETOPT_H_
#define LUD_GETOPT_H_

/* Short options — like POSIX getopt(3). */
int lud_getopt(int argc, char *const argv[], const char *optstring);

/* Long options — like POSIX getopt_long(3).
 *
 * Each lud_option entry describes one --long-name flag:
 *   .name      The long option name (without leading dashes).
 *   .has_arg   LUD_NO_ARG, LUD_REQUIRED_ARG, or LUD_OPTIONAL_ARG.
 *   .flag      If non-NULL, *flag is set to .val and 0 is returned.
 *              If NULL, .val is returned directly.
 *   .val       Value to store or return (typically a short-option char).
 *
 * The longopts array must be terminated by a zeroed entry.
 * If longindex is non-NULL, *longindex is set to the matched entry's index.
 *
 * Single-dash long options (e.g. -bios) are matched when the argument
 * starts with a single dash, is not a known short option or is more than
 * two characters long (so it cannot be a short flag + attached value).
 */

enum {
	LUD_NO_ARG       = 0,
	LUD_REQUIRED_ARG = 1,
	LUD_OPTIONAL_ARG = 2,
};

typedef struct lud_option {
	const char *name;
	int has_arg;
	int *flag;
	int val;
} lud_option;

int lud_getopt_long(int argc, char *const argv[], const char *optstring,
		    const lud_option *longopts, int *longindex);

extern const char *lud_optarg;
extern int lud_opterr, lud_optind, lud_optopt;

#endif
