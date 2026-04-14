/* lud_getopt.c : portable command-line argument processing */
/*
 * Copyright (c) 2020,2021 Jon Mayo <jon@rm-f.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "include/lud_getopt.h"
#include <stdio.h>
#include <string.h>

const char *lud_optarg = 0;
int lud_opterr = 1, lud_optind = 0, lud_optopt = 0;

static const char *cur = "", *progname;

static void
init_progname(int argc, char *const argv[])
{
	if (!lud_optind) {
		if (!argc) {
			progname = "";
		} else {
			progname = strrchr(argv[0], '/');
			if (!progname)
				progname = argv[0];
			else
				progname++;
		}
	}
}

/* Try to match a --long or -long option.
 * Returns the lud_option entry index on match, -1 otherwise.
 * Sets *out_arg to the =value portion if present, NULL otherwise. */
static int
match_long(const char *arg, const lud_option *longopts, const char **out_arg)
{
	const char *eq;
	int i;
	size_t namelen;

	*out_arg = NULL;

	/* find '=' separator if present */
	eq = strchr(arg, '=');
	namelen = eq ? (size_t)(eq - arg) : strlen(arg);

	for (i = 0; longopts[i].name; i++) {
		if (strncmp(arg, longopts[i].name, namelen) == 0 &&
		    longopts[i].name[namelen] == '\0') {
			if (eq)
				*out_arg = eq + 1;
			return i;
		}
	}

	return -1;
}

static int
process_short(int argc, char *const argv[], const char *optstring)
{
	const char *ip;

	/* current flag */
	lud_optopt = (int)(unsigned char)*cur++;

	/* locate flag in the optstring */
	ip = strchr(optstring, lud_optopt);
	if (!ip) {
		if (lud_opterr)
			fprintf(stderr, "%s: illegal option -- %c\n",
				progname, lud_optopt);
		return '?'; /* error: unknown flag */
	}

	/* does this option require an argument? */
	if (ip[1] == ':') {
		if (*cur) {
			/* use remaining portion of current argv as lud_optarg */
			lud_optarg = cur;
			cur = ""; /* at next entry, go to next lud_optind */
		} else {
			/* use next argv as the lud_optarg */
			lud_optind++;
			if (lud_optind >= argc) {
				if (lud_opterr)
					fprintf(stderr,
						"%s: option requires an argument -- %c\n",
						progname, lud_optopt);
				return ':'; /* error: missing argument */
			}
			lud_optarg = argv[lud_optind];
			cur = ""; /* at next entry, go to next lud_optind */
		}
	}

	return lud_optopt;
}

int
lud_getopt(int argc, char *const argv[], const char *optstring)
{
	return lud_getopt_long(argc, argv, optstring, NULL, NULL);
}

int
lud_getopt_long(int argc, char *const argv[], const char *optstring,
		const lud_option *longopts, int *longindex)
{
	const char *eq_arg = NULL;
	int idx = -1;

	init_progname(argc, argv);

	/* disable stderr output if optstring starts with ':' */
	if (*optstring == ':') {
		lud_opterr = 0;
		optstring++;
	}

	/* done processing flag series on current argument? */
	if (!*cur) {
		const char *arg;

		lud_optind++;

		if (lud_optind >= argc) {
			cur = "";
			return -1; /* done processing - end of arguments */
		}

		arg = argv[lud_optind];
		if (arg[0] != '-') {
			cur = "";
			return -1; /* done processing - not a flag */
		}

		/* "--" stops option processing */
		if (arg[1] == '-' && arg[2] == '\0') {
			lud_optind++;
			cur = "";
			return -1;
		}

		/* "--long-option" or "--long-option=value" */
		if (arg[1] == '-' && longopts) {
			idx = match_long(arg + 2, longopts, &eq_arg);
			if (idx >= 0)
				goto found_long;
			if (lud_opterr)
				fprintf(stderr, "%s: unrecognized option '%s'\n",
					progname, arg);
			lud_optopt = 0;
			return '?';
		}

		/* single-dash: try long match first for args like "-bios"
		 * A long match is attempted when longopts is provided and
		 * either:
		 *  - the arg is longer than 2 chars ("-bios", not "-b"), OR
		 *  - the char after '-' is not in optstring
		 * This lets "-bios" match the "bios" long option even when
		 * 'b' is also a valid short option. */
		if (longopts && arg[1] != '\0') {
			int try_long = (arg[2] != '\0') ||
				       !strchr(optstring, arg[1]);
			if (try_long) {
				idx = match_long(arg + 1, longopts, &eq_arg);
				if (idx >= 0)
					goto found_long;
			}
		}

		/* short option series */
		cur = arg + 1;
	}

	return process_short(argc, argv, optstring);

found_long:
	/* handle long option at longopts[idx] */
	lud_optarg = NULL;

	if (longopts[idx].has_arg == LUD_REQUIRED_ARG) {
		if (eq_arg) {
			lud_optarg = eq_arg;
		} else {
			lud_optind++;
			if (lud_optind >= argc) {
				if (lud_opterr)
					fprintf(stderr,
						"%s: option '--%s' requires an argument\n",
						progname, longopts[idx].name);
				return ':';
			}
			lud_optarg = argv[lud_optind];
		}
	} else if (longopts[idx].has_arg == LUD_OPTIONAL_ARG) {
		if (eq_arg)
			lud_optarg = eq_arg;
	} else {
		/* LUD_NO_ARG — complain if =value was given */
		if (eq_arg) {
			if (lud_opterr)
				fprintf(stderr,
					"%s: option '--%s' doesn't allow an argument\n",
					progname, longopts[idx].name);
			return '?';
		}
	}

	if (longindex)
		*longindex = idx;

	if (longopts[idx].flag) {
		*longopts[idx].flag = longopts[idx].val;
		return 0;
	}

	return longopts[idx].val;
}
