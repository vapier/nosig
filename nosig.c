/*
 * A generic signal blocking utility program akin to "nohup".
 *
 * Written by Mike Frysinger <vapier@gmail.com>
 * Released into the public domain.
 */

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HOMEPAGE "https://github.com/vapier/nosig/"

/* macOS doesn't support realtime signals as they were optional. */
#if defined(SIGRTMIN) && defined(SIGRTMAX)
# define USE_RT 1
#else
# define USE_RT 0
#endif

/*
 * Some random global variables.  Should limit this.
 */
static size_t verbose = 0;

/*
 * Exit statuses to use.
 * Make sure to never use any other value (e.g. "0" or "1").
 * This provides a reliable(ish) scripting interface.
 */
/* nosig processed a flag itself like --help.  Must not be used for anything else! */
#define EXIT_OK 0
/* The requested program was not executable. */
#define EXIT_PROG_NOT_EXEC 126
/* The requested program could not be found. */
#define EXIT_PROG_NOT_FOUND 127
/* nosig exited for any other reason. */
#define EXIT_ERR 125

/* Compiler hint that the func never returns. */
#define ATTR_NORETURN __attribute__((__noreturn__))

/* Return number of elements in the static array |x|. */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Return true when |s1| & |s2| strings are equal. */
#define streq(s1, s2) (strcmp(s1, s2) == 0)

/* Convert a number into an integer with error checking. */
static long xatoi(const char *s, int base)
{
	char *end;
	long ret = strtol(s, &end, base);
	if (*s && *end)
		errx(EXIT_ERR, "error: could not decode: %s", s);
	return ret;
}

struct pair {
	const char *name;
	int value;
};

/*
 * List of all signals binding symbolic names to numerical value.
 *
 * This is sorted somewhat arbitrarily so the number value is in order on an
 * x86_64/Linux system so the list_signals function displays in order.  Probably
 * should improve that function to do dynamic sorting itself at some point.
 * It's also sorted to give priority for certain signal names over others when
 * the names resolve to the same number.
 *
 * ifdef protection is used only for signal names not defined by POSIX.
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/signal.h.html
 * Non-POSIX targets are a non-goal of the project (i.e. Windows).  Additional
 * non-standard signals may be added if they're used by a common OS (i.e. BSD).
 *
 * NB: In a pinch, users may always user a signal by number, so listing the
 * symbolic name here is not super critical to use.
 *
 * NB: SIGRT{MIN,MAX} as specifically omitted from this list.  This only tracks
 * constant signals and SIGRT{MIN,MAX} are allowed to be dynamic as the OS is
 * allowed to reserve from that range thereby adjusting their value.  We parse
 * those signal names on the fly rather than use a lookup table.
 */
#define P(s) { #s, s }
static const struct pair signals[] = {
	P(SIGHUP),
	P(SIGINT),
	P(SIGQUIT),
	P(SIGILL),
	P(SIGTRAP),
	P(SIGABRT),
#ifdef SIGIOT
	P(SIGIOT),
#endif
	P(SIGBUS),
	P(SIGFPE),
	P(SIGKILL),
	P(SIGUSR1),
	P(SIGSEGV),
	P(SIGUSR2),
	P(SIGPIPE),
	P(SIGALRM),
	P(SIGTERM),
#ifdef SIGSTKFLT
	P(SIGSTKFLT),
#endif
	P(SIGCHLD),
	P(SIGCONT),
	P(SIGSTOP),
	P(SIGTSTP),
	P(SIGTTIN),
	P(SIGTTOU),
	P(SIGURG),
	P(SIGXCPU),
	P(SIGXFSZ),
	P(SIGVTALRM),
	P(SIGPROF),
#ifdef SIGWINCH
	P(SIGWINCH),
#endif
#ifdef SIGIO
	P(SIGIO),
#endif
	P(SIGPOLL),
#ifdef SIGPWR
	P(SIGPWR),
#endif
	P(SIGSYS),
#ifdef SIGEMT
	P(SIGEMT),
#endif
#ifdef SIGUNUSED
	P(SIGUNUSED),
#endif
};
#undef P

/* POSIX does not make it easy to figure out how many signals are supported. */
static int get_sigmax(void)
{
#if USE_RT
	return SIGRTMAX;
#else
	size_t i;
	int sig = 1;

	for (i = 0; i < ARRAY_SIZE(signals); ++i)
		if (sig < signals[i].value)
			sig = signals[i].value;

	return sig;
#endif
}

/* Turn a symbolic signal name from the user into a signal number. */
static int get_signal_num(const char *name)
{
	size_t i, off;

	if (name == NULL)
		errx(EXIT_ERR, "missing signal spec");

	/* The leading "SIG" is optional. */
	off = (strncmp(name, "SIG", 3) == 0) ? 0 : 3;

	/* Look up the name in the signal table. */
	for (i = 0; i < ARRAY_SIZE(signals); ++i)
		if (streq(&signals[i].name[off], name))
			return signals[i].value;

#if USE_RT
	/* Realtime signals are fun! */
	long rtrange = SIGRTMAX - SIGRTMIN;
	if (strncmp(name, &"SIGRTMIN"[off], 8 - off) == 0) {
		int ret = SIGRTMIN;
		switch (name[8 - off]) {
		case '\0':
			return ret;
		case '+': {
			long adj = xatoi(&name[8 - off], 10);
			if (adj > rtrange)
				errx(EXIT_ERR, "SIGRTMIN offset exceeds %zu", rtrange);
			return ret + adj;
		}
		default:
			errx(EXIT_ERR, "must be SIGRTMIN or SIGRTMIN+<number>");
		}
	} else if (strncmp(name, &"SIGRTMAX"[off], 8 - off) == 0) {
		int ret = SIGRTMAX;
		switch (name[8 - off]) {
		case '\0':
			return ret;
		case '-': {
			long adj = xatoi(&name[8 - off], 10);
			if (adj < -rtrange)
				errx(EXIT_ERR, "SIGRTMAX offset exceeds %zu", rtrange);
			return ret + adj;
		}
		default:
			errx(EXIT_ERR, "must be SIGRTMAX or SIGRTMAX-<number>");
		}
	}
#endif

	/* Maybe it's a number. */
	long signum = xatoi(name, 10);
	if (signum < 0)
		errx(EXIT_ERR, "only positive integers are allowed: %s", name);
	if (signum > get_sigmax())
		errx(EXIT_ERR, "signals greater than %i not supported", get_sigmax());
	return signum;
}

/* Return the symbolic signal name for |sig|. */
static const char *strsigname(int sig)
{
	size_t i;

	/* Look up standard signals first. */
	for (i = 0; i < ARRAY_SIZE(signals); ++i)
		if (signals[i].value == sig)
			return signals[i].name;

#if USE_RT
	/* Fallback to realtime signals. */
	if (sig == SIGRTMIN)
		return "SIGRTMIN";
	else if (sig == SIGRTMAX)
		return "SIGRTMAX";
	else if (sig > SIGRTMIN && sig < SIGRTMAX) {
		static char sigrt[] = "SIGRTMIN+xx";
		sprintf(&sigrt[9], "%i", sig);
		return sigrt;
	}
#endif

	return "SIG???";
}

/*
 * Helpers to set signal dispositions via sigaction.
 *
 * Passing sa_handler/SIG_IGN/SIG_DFL as an argument is difficult due to lack
 * of a standard typedef in POSIX.  Let the compiler deal with optimization :P.
 */
static void _sigaction_range(struct sigaction *sa, int first, int last)
{
	int sig;
	for (sig = first; sig <= last; ++sig) {
		if (sigaction(sig, sa, NULL)) {
			/* SIGKILL/SIGSTOP trigger EINVAL.  Ignore by default. */
			if (verbose || errno != EINVAL)
				warn("sigaction(%s[%i]) failed", strsigname(sig), sig);
		}
	}
}
static void set_sigaction_ignore_range(struct sigaction *sa, int first, int last)
{
	sa->sa_handler = SIG_IGN;
	_sigaction_range(sa, first, last);
}
static void set_sigaction_ignore(struct sigaction *sa, int sig)
{
	set_sigaction_ignore_range(sa, sig, sig);
}
static void set_sigaction_default_range(struct sigaction *sa, int first, int last)
{
	sa->sa_handler = SIG_DFL;
	_sigaction_range(sa, first, last);
}
static void set_sigaction_default(struct sigaction *sa, int sig)
{
	set_sigaction_default_range(sa, sig, sig);
}

/*
 * Helpers to set signal block masks.
 *
 * NB: The meaning of first/last is inverted from sigaction_range.  We use
 * sigfillset and then remove the [first,last] range rather than sigemptyset
 * and then adding the range.  This gives the C library a chance to filter out
 * any reserved realtime signals it might have.  This way our --block-all opts
 * will match behavior with manual --fill --block settings.
 */
static void sigprocmask_range(int how, int first, int last)
{
	int sig;
	sigset_t set;
	sigfillset(&set);
	for (sig = first; sig <= last; ++sig)
		sigdelset(&set, sig);
	if (sigprocmask(how, &set, 0))
		warn("sigprocmask_range()");
}

/* Rebind |fd| to |path| using file |flags|. */
static void redirect_io(int oldfd, const char *path, int flags)
{
	/* We use mode 666 to let umask apply. */
	int newfd = open(path, flags, 0666);
	if (newfd < 0)
		err(EXIT_ERR, "could not open %s", path);
	/* Pathological edge case: if newfd is already oldfd, do nothing. */
	if (newfd != oldfd) {
		if (dup2(newfd, oldfd) == -1)
			err(EXIT_ERR, "could not dup to %i", oldfd);
	}
}
static void redirect_input_from(const char *path)
{
	redirect_io(0, path, O_RDONLY);
}
static void redirect_output_to(int oldfd, const char *path)
{
	redirect_io(oldfd, path, O_WRONLY|O_CREAT);
}

/* Print a single signal with consistent output format/alignment. */
static void list_one_signal(const char *name, int value)
{
	/* 15 should be bigger than all signals we know about. */
	const int signame_width = 15;
	printf("%-*s %2i   %s\n", signame_width, name, value, strsignal(value));
}

/* Print all the known signals names to stdout. */
ATTR_NORETURN
static void list_signals(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(signals); ++i)
		list_one_signal(signals[i].name, signals[i].value);

#if USE_RT
	list_one_signal("SIGRTMIN", SIGRTMIN);
	for (i = 0; i <= (size_t)(SIGRTMAX - SIGRTMIN); ++i) {
		static char signame[] = "SIGRTMIN+xx";
		sprintf(&signame[9], "%zu", i);
		list_one_signal(signame, SIGRTMIN + i);
	}

	list_one_signal("SIGRTMAX", SIGRTMAX);
	for (i = 0; i <= (size_t)(SIGRTMAX - SIGRTMIN); ++i) {
		static char signame[] = "SIGRTMAX+xx";
		sprintf(&signame[9], "%zu", i);
		list_one_signal(signame, SIGRTMAX - i);
	}
#endif

	exit(EXIT_OK);
}

/* Show version info. */
ATTR_NORETURN
static void show_version(void)
{
#ifndef VERSION
# define VERSION "???"
#endif
	printf(
		"nohup v" VERSION "\n"
#if USE_RT
		"Realtime signals supported\n"
#else
		"OS missing realtime signal support\n"
#endif
		HOMEPAGE "\n"
		"Written by Mike Frysinger <vapier@gmail.com>\n"
	);

	exit(EXIT_OK);
}

/* Show signal status info. */
ATTR_NORETURN
static void show_status(void)
{
	int sig;
	int off = verbose <= 1 ? 3 : 0;

	/* Dump signal dispositions. */
	struct sigaction sa;
	if (verbose)
		printf("disp:");
	for (sig = 1; sig <= get_sigmax(); ++sig) {
		bool sig_ign = false, sig_dfl = false;
		if (sigaction(sig, NULL, &sa)) {
			if (errno != EINVAL)
				warn("sigaction()");
			else
				sig_dfl = true;
		} else {
			sig_ign = sa.sa_handler == SIG_IGN;
			sig_dfl = sa.sa_handler == SIG_DFL;
		}

		printf(" %s", sig_ign ? "i" : sig_dfl ? "d" : "?");
		if (verbose)
			printf("%s[%i]", &strsigname(sig)[off], sig);
		else
			printf("%i", sig);
	}
	printf("\n");

	/* Dump signal block mask. */
	sigset_t set;
	if (sigprocmask(0, NULL, &set))
		err(EXIT_ERR, "sigprocmask()");
	if (verbose)
		printf("mask:");
	for (sig = 1; sig <= get_sigmax(); ++sig) {
		printf(" %s", sigismember(&set, sig) ? "b" : "u");
		if (verbose)
			printf("%s[%i]", &strsigname(sig)[off], sig);
		else
			printf("%i", sig);
	}
	printf("\n");

	exit(EXIT_OK);
}

/* Command line option settings. */
#define short_options "a:d:efbusI:D:vlVh"
#define a_argument required_argument
enum {
	ONLY_LONG_OPTS_BASE = 0x100,
	OPT_RESET_ALL,
	OPT_SHOW_STATUS,
	OPT_IGNORE_ALL,
	OPT_IGNORE_ALL_STD,
	OPT_IGNORE_ALL_RT,
	OPT_DEFAULT_ALL,
	OPT_DEFAULT_ALL_STD,
	OPT_DEFAULT_ALL_RT,
	OPT_BLOCK_ALL,
	OPT_BLOCK_ALL_STD,
	OPT_BLOCK_ALL_RT,
	OPT_UNBLOCK_ALL,
	OPT_UNBLOCK_ALL_STD,
	OPT_UNBLOCK_ALL_RT,
	OPT_STDIN,
	OPT_STDOUT,
	OPT_STDERR,
	OPT_OUTPUT,
	OPT_NULL_IO,
};
static const struct option options[] = {
	{"reset",             no_argument, NULL, OPT_RESET_ALL},

	{"ignore",             a_argument, NULL, 'I'},
	{"ignore-all",        no_argument, NULL, OPT_IGNORE_ALL},
	{"ignore-all-std",    no_argument, NULL, OPT_IGNORE_ALL_STD},
#if USE_RT
	{"ignore-all-rt",     no_argument, NULL, OPT_IGNORE_ALL_RT},
#endif
	{"default",            a_argument, NULL, 'D'},
	{"default-all",       no_argument, NULL, OPT_DEFAULT_ALL},
	{"default-all-std",   no_argument, NULL, OPT_DEFAULT_ALL_STD},
#if USE_RT
	{"default-all-rt",    no_argument, NULL, OPT_DEFAULT_ALL_RT},
#endif

	{"add",                a_argument, NULL, 'a'},
	{"del",                a_argument, NULL, 'd'},
	{"empty",             no_argument, NULL, 'e'},
	{"fill",              no_argument, NULL, 'f'},

	{"block",             no_argument, NULL, 'b'},
	{"unblock",           no_argument, NULL, 'u'},
	{"set",               no_argument, NULL, 's'},
	{"block-all",         no_argument, NULL, OPT_BLOCK_ALL},
	{"block-all-std",     no_argument, NULL, OPT_BLOCK_ALL_STD},
#if USE_RT
	{"block-all-rt",      no_argument, NULL, OPT_BLOCK_ALL_RT},
#endif
	{"unblock-all",       no_argument, NULL, OPT_UNBLOCK_ALL},
	{"unblock-all-std",   no_argument, NULL, OPT_UNBLOCK_ALL_STD},
#if USE_RT
	{"unblock-all-rt",    no_argument, NULL, OPT_UNBLOCK_ALL_RT},
#endif

	{"stdin",              a_argument, NULL, OPT_STDIN},
	{"stdout",             a_argument, NULL, OPT_STDOUT},
	{"stderr",             a_argument, NULL, OPT_STDERR},
	{"output",             a_argument, NULL, OPT_OUTPUT},
	{"null-io",           no_argument, NULL, OPT_NULL_IO},

	{"verbose",           no_argument, NULL, 'v'},
	{"show-status",       no_argument, NULL, OPT_SHOW_STATUS},
	{"list",              no_argument, NULL, 'l'},
	{"version",           no_argument, NULL, 'V'},
	{"help",              no_argument, NULL, 'h'},

	{NULL, 0, NULL, 0},
};

/* Help text for the above options.  Must be in the same order! */
static const char * const help_text[] = {
	"Reset all signals: unblock & set to default dispositions",

	"Ignore one signal",
	"Ignore all signals",
	"Ignore all standard signals",
#if USE_RT
	"Ignore all realtime signals",
#endif
	"Reset one signal disposition to the default",
	"Reset all signal dispositions to their default",
	"Reset all standard signal dispositions to their default",
#if USE_RT
	"Reset all realtime signal dispositions to their default",
#endif

	"Add signal to the current signal set",
	"Delete signal from the current signal set",
	"Empty out the current signal set",
	"Fill the current signal set",

	"Add the current signal set to the block mask",
	"Remove the current signal set from the block mask",
	"Set the block mask to the current signal set",
	"Block all signals (ignores current signal set)",
	"Block all standard signals (ignores current signal set)",
#if USE_RT
	"Block all realtime signals (ignores current signal set)",
#endif
	"Unblock all signals (ignores current signal set)",
	"Unblock all standard signals (ignores current signal set)",
#if USE_RT
	"Unblock all realtime signals (ignores current signal set)",
#endif

	"Redirect stdin from the specified path",
	"Redirect stdout to the specified path",
	"Redirect stderr to the specified path",
	"Redirect stdout & stderr to the specified path",
	"Redirect stdin/stdout/stderr to /dev/null",

	"Display verbose internal nosig output",
	"Display current signal settings (meant for debugging)",
	"List all known signals",
	"Show version info and exit",
	"This help text",
};

/* Make sure the sizes are the same to avoid memory errors at runtime. */
static_assert(ARRAY_SIZE(options) == ARRAY_SIZE(help_text) + 1,
              "command line options & help text out of sync!");

/* Display command line options and exit. */
ATTR_NORETURN
static void show_usage(int status)
{
	FILE *fp = status ? stderr : stdout;
	size_t i;

	fprintf(
		fp,
		"Usage: nosig [options] <program> [program args]\n"
		"\n"
		"Like `nohup`, but more advanced signal management.\n"
		"Signals are specified by name e.g. SIGTERM or TERM.\n"
#if USE_RT
		"Realtime signals are specified as offsets of SIGRTMIN or SIGRTMAX.\n"
#endif
		"\n"
		"The options fall into one of three buckets:\n"
		" - Oneshots (sigaction(2)): --ignore --default\n"
		" - Set management (sigsetops(3)): --add --del --empty --fill\n"
		" - Set usage (sigprocmask(2)): --block --unblock --set\n"
		"You should manage the set, then use the set.  This may be repeated!\n"
		"\n"
		"Options:\n"
	);

	/* Print out all the options dynamically, and with alignment. */
	const int minpad = 25;
	for (i = 0; i < ARRAY_SIZE(help_text); ++i) {
		int pad;

		if (options[i].val < 0x100)
			pad = fprintf(fp, "  -%c, ", options[i].val);
		else
			pad = fprintf(fp, "      ");
		pad += fprintf(fp, "--%s ", options[i].name);
		if (options[i].has_arg == a_argument)
			pad += fprintf(fp, "<arg> ");
		/* This assert is more of a reminder to update the constant. */
		assert(pad <= minpad);
		fprintf(fp, "%*s%s\n", minpad - pad, "", help_text[i]);
	}

	fprintf(
		fp,
		"\n"
		"For more details (and examples), see the nosig(1) man page.\n"
		"Project homepage: " HOMEPAGE "\n"
	);

	exit(status);
}

int main(int argc, char *argv[])
{
	int c;
	sigset_t set;
	struct sigaction sa;

	sigemptyset(&set);

	memset(&sa, 0, sizeof(sa));
	sigfillset(&sa.sa_mask);

	/* Process the command line. */
	while ((c = getopt_long(argc, argv, "+" short_options, options, NULL)) != -1) {
		switch (c) {
		case OPT_RESET_ALL:
			sigprocmask_range(SIG_UNBLOCK, 0, -1);
			set_sigaction_default_range(&sa, 1, get_sigmax());
			break;
		case 'v':
			++verbose;
			break;

		case 'a':
			sigaddset(&set, get_signal_num(optarg));
			break;
		case 'd':
			sigdelset(&set, get_signal_num(optarg));
			break;
		case 'e':
			sigemptyset(&set);
			break;
		case 'f':
			sigfillset(&set);
			break;

		case 'b':
			if (sigprocmask(SIG_BLOCK, &set, 0))
				warn("sigprocmask(SIG_BLOCK)");
			break;
		case 'u':
			if (sigprocmask(SIG_UNBLOCK, &set, 0))
				warn("sigprocmask(SIG_UNBLOCK)");
			break;
		case 's':
			if (sigprocmask(SIG_SETMASK, &set, 0))
				warn("sigprocmask(SIG_SETMASK)");
			break;
#if USE_RT
		case OPT_BLOCK_ALL_RT:
			sigprocmask_range(SIG_BLOCK, 1, SIGRTMIN - 1);
			break;
		case OPT_BLOCK_ALL_STD:
			sigprocmask_range(SIG_BLOCK, SIGRTMIN, SIGRTMAX);
			break;
#else
		case OPT_BLOCK_ALL_STD:
#endif
		case OPT_BLOCK_ALL:
			sigprocmask_range(SIG_BLOCK, 0, -1);
			break;
#if USE_RT
		case OPT_UNBLOCK_ALL_RT:
			sigprocmask_range(SIG_UNBLOCK, 1, SIGRTMIN - 1);
			break;
		case OPT_UNBLOCK_ALL_STD:
			sigprocmask_range(SIG_UNBLOCK, SIGRTMIN, SIGRTMAX);
			break;
#else
		case OPT_UNBLOCK_ALL_STD:
#endif
		case OPT_UNBLOCK_ALL:
			sigprocmask_range(SIG_UNBLOCK, 0, -1);
			break;

		case 'I':
			set_sigaction_ignore(&sa, get_signal_num(optarg));
			break;
#if USE_RT
		case OPT_IGNORE_ALL_RT:
			set_sigaction_ignore_range(&sa, SIGRTMIN, SIGRTMAX);
			break;
		case OPT_IGNORE_ALL_STD:
			set_sigaction_ignore_range(&sa, 1, SIGRTMIN - 1);
			break;
#else
		case OPT_IGNORE_ALL_STD:
#endif
		case OPT_IGNORE_ALL:
			set_sigaction_ignore_range(&sa, 1, get_sigmax());
			break;
		case 'D':
			set_sigaction_default(&sa, get_signal_num(optarg));
			break;
#if USE_RT
		case OPT_DEFAULT_ALL_RT:
			set_sigaction_default_range(&sa, SIGRTMIN, SIGRTMAX);
			break;
		case OPT_DEFAULT_ALL_STD:
			set_sigaction_default_range(&sa, 1, SIGRTMIN - 1);
			break;
#else
		case OPT_DEFAULT_ALL_STD:
#endif
		case OPT_DEFAULT_ALL:
			set_sigaction_default_range(&sa, 1, get_sigmax());
			break;

		case OPT_STDIN:
			redirect_input_from(optarg);
			break;
		case OPT_STDOUT:
			redirect_output_to(1, optarg);
			break;
		case OPT_STDERR:
			redirect_output_to(2, optarg);
			break;
		case OPT_OUTPUT:
			redirect_output_to(1, optarg);
			if (dup2(1, 2) == -1)
				err(EXIT_ERR, "Could not dupe stdout to stderr");
			break;
		case OPT_NULL_IO:
			redirect_input_from("/dev/null");
			redirect_output_to(1, "/dev/null");
			redirect_output_to(2, "/dev/null");
			break;

		case OPT_SHOW_STATUS:
			show_status();
		case 'l':
			list_signals();
		case 'V':
			show_version();
		case 'h':
			show_usage(EXIT_OK);
		default:
			show_usage(EXIT_ERR);
		}
	}

	/* Shift the command line to the user's program to exec. */
	argc -= optind;
	argv += optind;

	if (argc) {
		execvp(argv[0], argv);
		/*
		 * Use exit status like POSIX/bash/nohup/env/etc...
		 * https://pubs.opengroup.org/onlinepubs/009695399/utilities/env.html#tag_04_43_14
		 */
		int status;
		if (errno == ENOENT)
			status = EXIT_PROG_NOT_FOUND;
		else if (errno == EACCES)
			status = EXIT_PROG_NOT_EXEC;
		else
			status = EXIT_ERR;
		err(status, "%s", argv[0]);
	} else
		errx(EXIT_ERR, "missing program to run");
}
