/* lud_getopt.h : portable command-line argument processing - public domain */
#ifndef LUD_GETOPT_H_
#define LUD_GETOPT_H_
int lud_getopt(int argc, char *const argv[], const char *optstring);
extern const char *lud_optarg;
extern int lud_opterr, lud_optind, lud_optopt;
#endif
