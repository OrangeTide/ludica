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

int
lud_getopt(int argc, char *const argv[], const char *optstring)
{
	static const char *cur = "", *progname;
	const char *ip;

	/* guess at our program name using basename of argv[0] */
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

	/* disable stderr output if optstring starts with ':' */
	if (*optstring == ':') {
		lud_opterr = 0;
		optstring++;
	}

	/* done processing flag series on current argument? */
	if (!*cur) {
		lud_optind++;

		if (lud_optind >= argc) {
			cur = "";
			return -1; /* done processing - end of arguments */
		}

		/* start a flag series at the new optind */
		cur = argv[lud_optind];
		if (*cur != '-') {
			cur = "";
			return -1; /* done processing - no more flag series */
		}
		/* end flags if argument is exactly "--" */
		if (cur[1] == '-' && cur[2] == 0) {
			lud_optind++;
			cur = "";
			return -1; /* done processing - no more flag series */
		}
		cur++;
	}

	/* current flag */
	lud_optopt = (int)(unsigned char)*cur++;

	/* locate flag in the optstring */
	ip = strchr(optstring, lud_optopt);
	if (!ip) {
		if (lud_opterr)
			fprintf(stderr, "%s: illegal option -- %c\n", progname, lud_optopt);
		return '?'; /* error: unknown flag */
	}

	/* does this optstring have arguments? */
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
					fprintf(stderr, "%s: option requires an argument -- %c\n", progname, lud_optopt);
				return ':'; /* error: missing argument */
			}
			lud_optarg = argv[lud_optind];
			cur = ""; /* at next entry, go to next lud_optind */
		}
	}

	return lud_optopt;
}
