#ifndef PROCSHELL_H
#define PROCSHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/types.h>
#include <signal.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <time.h>

#include <sys/wait.h>

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define RIGHT "\033[C"
#define LEFT "\033[D"
#define UP "\033[1A"
#define DOWN "\033[1B"
#define CLEAR_LINE "\033[2K\r"

#define PROMPT "$ "
#define AI_PROMPT "> "

/* Procshell is POSIX-only (Linux / macOS) */
#define SEP ":"
#define PATH_SEPARATOR "/"

/* Exit-status conventions (same as bash) */
#define STATUS_FAILURE 1              /* generic failure: bad redirect, failed builtin, ... */
#define STATUS_SIGNAL_BASE 128        /* a command killed by signal N returns 128 + N */
#define STATUS_INTERRUPTED (STATUS_SIGNAL_BASE + SIGINT) /* Ctrl+C -> 130 */

#define MAX_AI_COMMANDS 512
#define MAX_AI_QUESTIONS 16
#define MAX_AI_ATTEMPTS 4
#define AI_HISTORY_COUNT 5

/* Shared global state (defined in main.c) */
extern volatile sig_atomic_t whichSignal; /* last signal seen by the handler */
extern volatile int termCols;             /* terminal width */
extern sigset_t shellOrigMask;            /* signal mask at startup, restored in children */
extern char *procshellHome;               /* folder containing the procshell binary */
extern char *procshellDataDir;            /* ~/.procshell - AI temp files live here */
extern time_t procshellStartTime;         /* when the shell started (for sysinfo) */

#include "types/parser.h"
#include "types/ai.h"
#include "basicHelpers.h"
#include "builtins.h"
#include "executors.h"
#include "helpers.h"
#include "input.h"
#include "parser.h"
#include "forgettingDLL.h"
#include "aiParser.h"
#include "mycommands.h"

#endif