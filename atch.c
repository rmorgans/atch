#include "atch.h"

const char copyright[] = PACKAGE_NAME " - version " PACKAGE_VERSION;

/* Returns the basename of the current session socket path. */
const char *session_shortname(void)
{
	const char *p = strrchr(sockname, '/');
	return p ? p + 1 : sockname;
}

/* Returns the directory where session sockets are stored. */
void get_session_dir(char *buf, size_t size)
{
	const char *home = getenv("HOME");
	const char *base = strrchr(progname, '/');

	base = base ? base + 1 : progname;
	if (home && *home)
		snprintf(buf, size, "%s/.cache/%s", home, base);
	else
		snprintf(buf, size, "/tmp/.%s-%d", base, (int)getuid());
}

/* argv[0] from the program */
char *progname;
/* The name of the passed in socket. */
char *sockname;
/* The character used for detaching. Defaults to '^\' */
int detach_char = '\\' - 64;
/* 1 if we should not interpret the suspend character. */
int no_suspend;
/* The default redraw method. Initially set to winch. */
int redraw_method = REDRAW_WINCH;
/* The default clear method. Initially set to none. */
int clear_method = CLEAR_NONE;
int quiet = 0;
/* 1 if we should not send ansi sequences to the terminal */
int no_ansiterm = 0;

/*
** The original terminal settings. Shared between the master and attach
** processes. The master uses it to initialize the pty, and the attacher uses
** it to restore the original settings.
*/
struct termios orig_term;
int dont_have_tty;

static void usage()
{
	printf(PACKAGE_NAME " - version %s, compiled on %s at %s.\n"
	       "Usage: " PACKAGE_NAME " -a <session> <options>\n"
	       "       " PACKAGE_NAME " -A <session> <options> [command...]\n"
	       "       " PACKAGE_NAME " -c <session> <options> [command...]\n"
	       "       " PACKAGE_NAME " -n <session> <options> [command...]\n"
	       "       " PACKAGE_NAME " -N <session> <options> [command...]\n"
	       "       " PACKAGE_NAME " -p <session>\n"
	       "       " PACKAGE_NAME " -k <session>\n"
	       "       " PACKAGE_NAME " -l\n"
	       "       " PACKAGE_NAME " -i\n"
	       "Modes:\n"
	       "  -a\t\tAttach to the specified session.\n"
	       "  -A\t\tAttach to the specified session, or create it if it\n"
	       "\t\t  does not exist, running the specified command.\n"
	       "  -c\t\tCreate a new session and run the specified command.\n"
	       "  -n\t\tCreate a new session and run the specified command "
	       "detached.\n"
	       "  -N\t\tCreate a new session and run the specified command "
	       "detached,\n"
	       "\t\t  and have " PACKAGE_NAME " run in the foreground.\n"
	       "  -p\t\tCopy the contents of standard input to the specified\n"
	       "\t\t  session.\n"
	       "  -k\t\tGracefully stop the specified session (SIGTERM, then\n"
	       "\t\t  SIGKILL if it does not exit within 5 seconds).\n"
	       "  -l\t\tList all sessions.\n"
	       "  -i\t\tPrint current session name and exit 0 if inside a\n"
	       "\t\t  session, exit 1 silently if not.\n"
	       "Options:\n"
	       "  -e <char>\tSet the detach character to <char>, defaults "
	       "to ^\\.\n"
	       "  -E\t\tDisable the detach character.\n"
	       "  -r <method>\tSet the redraw method to <method>. The "
	       "valid methods are:\n"
	       "\t\t     none: Don't redraw at all.\n"
	       "\t\t   ctrl_l: Send a Ctrl L character to the program.\n"
	       "\t\t    winch: Send a WINCH signal to the program.\n"
	       "  -R <method>\tSet the clear method to <method>. The "
	       "valid methods are:\n"
	       "\t\t     none: Don't clear at all.\n"
	       "\t\t     move: Move to last line (default behaviour).\n"
	       "  -z\t\tDisable processing of the suspend key.\n"
	       "  -q\t\tDisable printing of additional messages.\n"
	       "  -t\t\tDisable VT100 assumptions.\n"
	       "\nURL: " PACKAGE_URL "\n\n", PACKAGE_VERSION, __DATE__,
	       __TIME__);
	exit(0);
}

int main(int argc, char **argv)
{
	int mode = 0;

	/* Save the program name */
	progname = argv[0];
	++argv;
	--argc;

	/* Parse the arguments */
	if (argc < 1)
		usage();
	if (argc >= 1 && **argv == '-') {
		if (strncmp(*argv, "--help", strlen(*argv)) == 0 ||
		    strcmp(*argv, "-h") == 0)
			usage();
		else if (strncmp(*argv, "--version", strlen(*argv)) == 0) {
			printf(PACKAGE_NAME
			       " - version %s, compiled on %s at %s.\n",
			       PACKAGE_VERSION, __DATE__, __TIME__);
			return 0;
		}

		mode = argv[0][1];
		if (mode == '?')
			usage();
		else if (mode == 'l') {
			return list_main();
		} else if (mode == 'i') {
			const char *s = getenv(SESSION_ENVVAR);
			if (!s || !*s)
				return 1;
			/* Print just the session name, not the full socket path. */
			const char *name = strrchr(s, '/');
			printf("%s\n", name ? name + 1 : s);
			return 0;
		} else if (mode != 'a' && mode != 'c' && mode != 'n' &&
			   mode != 'A' && mode != 'N' && mode != 'p' &&
			   mode != 'k') {
			printf("%s: Invalid mode '-%c'\n", progname, mode);
			printf("Try '%s --help' for more information.\n",
			       progname);
			return 1;
		}
	}
	if (!mode) {
		printf("%s: No mode was specified.\n", progname);
		printf("Try '%s --help' for more information.\n", progname);
		return 1;
	}
	++argv;
	--argc;

	if (argc < 1) {
		printf("%s: No session was specified.\n", progname);
		printf("Try '%s --help' for more information.\n", progname);
		return 1;
	}
	sockname = *argv;
	++argv;
	--argc;

	/* If no path separator, expand to ~/.{progname}/{sockname} */
	if (strchr(sockname, '/') == NULL) {
		char dir[512];
		size_t fulllen;
		char *full;

		get_session_dir(dir, sizeof(dir));
		/* Ensure parent (~/.cache) exists before the leaf dir. */
		{
			char *slash = strrchr(dir, '/');
			if (slash) {
				*slash = '\0';
				mkdir(dir, 0700);
				*slash = '/';
			}
		}
		mkdir(dir, 0700);
		fulllen = strlen(dir) + 1 + strlen(sockname);
		full = malloc(fulllen + 1);
		snprintf(full, fulllen + 1, "%s/%s", dir, sockname);
		sockname = full;
	}

	if (mode == 'p') {
		if (argc > 0) {
			printf("%s: Invalid number of arguments.\n", progname);
			printf("Try '%s --help' for more information.\n",
			       progname);
			return 1;
		}
		return push_main();
	}

	if (mode == 'k') {
		if (argc > 0) {
			printf("%s: Invalid number of arguments.\n", progname);
			printf("Try '%s --help' for more information.\n",
			       progname);
			return 1;
		}
		return kill_main();
	}

	while (argc >= 1 && **argv == '-') {
		char *p;

		if (strcmp(argv[0], "--") == 0) {
			++argv;
			--argc;
			break;
		}

		for (p = argv[0] + 1; *p; ++p) {
			if (*p == 'E')
				detach_char = -1;
			else if (*p == 'z')
				no_suspend = 1;
			else if (*p == 'q')
				quiet = 1;
			else if (*p == 't')
				no_ansiterm = 1;
			else if (*p == 'e') {
				++argv;
				--argc;
				if (argc < 1) {
					printf("%s: No escape character "
					       "specified.\n", progname);
					printf("Try '%s --help' for more "
					       "information.\n", progname);
					return 1;
				}
				if (argv[0][0] == '^' && argv[0][1]) {
					if (argv[0][1] == '?')
						detach_char = '\177';
					else
						detach_char = argv[0][1] & 037;
				} else
					detach_char = argv[0][0];
				break;
			} else if (*p == 'r') {
				++argv;
				--argc;
				if (argc < 1) {
					printf("%s: No redraw method "
					       "specified.\n", progname);
					printf("Try '%s --help' for more "
					       "information.\n", progname);
					return 1;
				}
				if (strcmp(argv[0], "none") == 0)
					redraw_method = REDRAW_NONE;
				else if (strcmp(argv[0], "ctrl_l") == 0)
					redraw_method = REDRAW_CTRL_L;
				else if (strcmp(argv[0], "winch") == 0)
					redraw_method = REDRAW_WINCH;
				else {
					printf("%s: Invalid redraw method "
					       "specified.\n", progname);
					printf("Try '%s --help' for more "
					       "information.\n", progname);
					return 1;
				}
				break;
			} else if (*p == 'R') {
				++argv;
				--argc;
				if (argc < 1) {
					printf("%s: No clear method "
					       "specified.\n", progname);
					printf("Try '%s --help' for more "
					       "information.\n", progname);
					return 1;
				}
				if (strcmp(argv[0], "none") == 0)
					clear_method = CLEAR_NONE;
				else if (strcmp(argv[0], "move") == 0)
					clear_method = CLEAR_MOVE;
				else {
					printf("%s: Invalid clear method "
					       "specified.\n", progname);
					printf("Try '%s --help' for more "
					       "information.\n", progname);
					return 1;
				}
				break;
			} else {
				printf("%s: Invalid option '-%c'\n",
				       progname, *p);
				printf
				    ("Try '%s --help' for more information.\n",
				     progname);
				return 1;
			}
		}
		++argv;
		--argc;
	}

	if (mode != 'a' && argc < 1) {
		const char *shell = getenv("SHELL");
		if (!shell || !*shell) {
			struct passwd *pw = getpwuid(getuid());
			if (pw && pw->pw_shell && *pw->pw_shell)
				shell = pw->pw_shell;
		}
		if (!shell || !*shell)
			shell = "/bin/sh";
		static char *shell_argv[2];
		shell_argv[0] = (char *)shell;
		shell_argv[1] = NULL;
		argv = shell_argv;
	}

	/* Save the original terminal settings. */
	if (tcgetattr(0, &orig_term) < 0) {
		memset(&orig_term, 0, sizeof(struct termios));
		dont_have_tty = 1;
	}

	if (dont_have_tty && mode != 'n' && mode != 'N') {
		printf("%s: attaching to a session requires a terminal.\n",
		       progname);
		return 1;
	}

	if (mode == 'a') {
		if (argc > 0) {
			printf("%s: Invalid number of arguments.\n", progname);
			printf("Try '%s --help' for more information.\n",
			       progname);
			return 1;
		}
		return attach_main(0);
	} else if (mode == 'n')
		return master_main(argv, 0, 0);
	else if (mode == 'N')
		return master_main(argv, 0, 1);
	else if (mode == 'c') {
		if (master_main(argv, 1, 0) != 0)
			return 1;
		return attach_main(0);
	} else if (mode == 'A') {
		/* Try to attach first. If that doesn't work, create a new
		 ** socket. */
		if (attach_main(1) != 0) {
			if (errno == ECONNREFUSED || errno == ENOENT) {
				int saved_errno = errno;

				/* Show previous session output before the new one starts. */
				replay_session_log(saved_errno);
				if (saved_errno == ECONNREFUSED)
					unlink(sockname);
				if (master_main(argv, 1, 0) != 0)
					return 1;
			}
			return attach_main(0);
		}
	}
	return 0;
}

char const *clear_csi_data(void)
{
	if (no_ansiterm || clear_method == CLEAR_NONE)
		return "\r\n";
	/* CLEAR_MOVE / CLEAR_UNSPEC: move cursor to bottom of screen */
	return "\033[999H\r\n";
}

/* Write buf to fd handling partial writes. Exit on failure. */
void write_buf_or_fail(int fd, const void *buf, size_t count)
{
	while (count != 0) {
		ssize_t ret = write(fd, buf, count);

		if (ret >= 0) {
			buf = (const char *)buf + ret;
			count -= ret;
		} else if (ret < 0 && errno == EINTR)
			continue;
		else {
			if (session_start) {
				char age[32];
				session_age(age, sizeof(age));
				printf
				    ("%s[%s: session '%s' write failed after %s]\r\n",
				     clear_csi_data(), progname,
				     session_shortname(), age);
			} else {
				printf("%s[%s: write failed]\r\n",
				       clear_csi_data(), progname);
			}
			exit(1);
		}
	}
}

/* Write pkt to fd. Exit on failure. */
void write_packet_or_fail(int fd, const struct packet *pkt)
{
	while (1) {
		ssize_t ret = write(fd, pkt, sizeof(struct packet));

		if (ret == sizeof(struct packet))
			return;
		else if (ret < 0 && errno == EINTR)
			continue;
		else {
			if (session_start) {
				char age[32];
				session_age(age, sizeof(age));
				printf
				    ("%s[%s: session '%s' write failed after %s]\r\n",
				     clear_csi_data(), progname,
				     session_shortname(), age);
			} else {
				printf("%s[%s: write failed]\r\n",
				       clear_csi_data(), progname);
			}
			exit(1);
		}
	}
}
