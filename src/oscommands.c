/*
 * ProcShell's process and OS commands.
 *
 *   sysinfo   -> uname(), sysconf(), getloadavg(), getrlimit()     OS, CPUs, memory, load
 *   procinfo  -> getpid(), getpgid(), getsid(), getpriority(),     ids, priority, limits of a process
 *                getrlimit(), kill(pid, 0)
 *   signal    -> kill()                                            send a signal to a process / group
 *   timeit    -> runs a command line through the executor and      real time, CPU time, peak memory
 *                reads the wait4() totals it collected
 *   history   -> walks the history doubly linked list              numbered command history
 */

#include <pwd.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include "procshell.h"

extern ForgettingDoublyLinkedList *history;

/* ---------- small helpers ---------- */

static bool colorOn(void)
{
  return isatty(STDOUT_FILENO);
}

static const char *bold(void) { return colorOn() ? "\033[1m" : ""; }
static const char *cyan(void) { return colorOn() ? "\033[1;36m" : ""; }
static const char *reset(void) { return colorOn() ? "\033[0m" : ""; }

static void row(const char *label, const char *value)
{
  printf("  %s%-16s%s %s\n", bold(), label, reset(), value);
}

/* 1536 -> "1.5 KB" */
static void bytesToText(double bytes, char *out, size_t outLen)
{
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit = 0;
  while (bytes >= 1024 && unit < 4)
  {
    bytes /= 1024;
    unit++;
  }
  snprintf(out, outLen, unit == 0 ? "%.0f %s" : "%.1f %s", bytes, units[unit]);
}

/* 3725 -> "1h 2m 5s" */
static void secondsToText(long seconds, char *out, size_t outLen)
{
  long h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
  if (h > 0)
    snprintf(out, outLen, "%ldh %ldm %lds", h, m, s);
  else if (m > 0)
    snprintf(out, outLen, "%ldm %lds", m, s);
  else
    snprintf(out, outLen, "%lds", s);
}

static void limitToText(int resource, char *out, size_t outLen, bool asBytes)
{
  struct rlimit rl;
  if (getrlimit(resource, &rl) == -1)
  {
    snprintf(out, outLen, "?");
    return;
  }
  if (rl.rlim_cur == RLIM_INFINITY)
  {
    snprintf(out, outLen, "unlimited");
    return;
  }
  if (asBytes)
    bytesToText((double)rl.rlim_cur, out, outLen);
  else
    snprintf(out, outLen, "%llu", (unsigned long long)rl.rlim_cur);
}

/* Reads a whole decimal number; false if the text is not one. */
static bool parsePid(const char *text, pid_t *pid)
{
  if (!text || !*text)
    return false;
  char *end;
  long value = strtol(text, &end, 10);
  if (*end != '\0' || value <= 0 || value > INT_MAX)
    return false;
  *pid = (pid_t)value;
  return true;
}

/* ---------- sysinfo ---------- */

int commandSysinfo(char **args)
{
  (void)args;
  char buf[256];

  printf("%sSystem information%s\n\n", cyan(), reset());

  struct utsname u;
  if (uname(&u) == 0)
  {
    snprintf(buf, sizeof(buf), "%s %s", u.sysname, u.release);
    row("OS", buf);
    row("Architecture", u.machine);
    row("Hostname", u.nodename);
  }

  struct passwd *pw = getpwuid(getuid());
  snprintf(buf, sizeof(buf), "%s (uid %d)", pw ? pw->pw_name : "?", (int)getuid());
  row("User", buf);

  long cpus = sysconf(_SC_NPROCESSORS_ONLN);
  snprintf(buf, sizeof(buf), "%ld online", cpus);
  row("CPUs", buf);

  long pages = sysconf(_SC_PHYS_PAGES);
  long pageSize = sysconf(_SC_PAGESIZE);
  if (pages > 0 && pageSize > 0)
  {
    bytesToText((double)pages * (double)pageSize, buf, sizeof(buf));
    row("Physical memory", buf);
  }
  snprintf(buf, sizeof(buf), "%ld bytes", pageSize);
  row("Page size", buf);

  double load[3];
  if (getloadavg(load, 3) == 3)
  {
    snprintf(buf, sizeof(buf), "%.2f  %.2f  %.2f   (1, 5, 15 min)", load[0], load[1], load[2]);
    row("Load average", buf);
  }

  printf("\n%sProcShell%s\n\n", cyan(), reset());
  // sysinfo runs in a child forked from the shell, so the shell is our parent
  pid_t shellPid = getppid();
  snprintf(buf, sizeof(buf), "%d  (process group %d)", (int)shellPid, (int)getpgid(shellPid));
  row("Shell PID", buf);
  secondsToText((long)difftime(time(NULL), procshellStartTime), buf, sizeof(buf));
  row("Running for", buf);
  snprintf(buf, sizeof(buf), "%d (max %d)", history ? history->size : 0, history ? history->capacity : 0);
  row("History entries", buf);
  limitToText(RLIMIT_NOFILE, buf, sizeof(buf), false);
  row("Max open files", buf);
  return 0;
}

/* ---------- procinfo ---------- */

/*
 * procinfo runs inside the shell itself (no fork), so with no argument it describes
 * the ProcShell process: its ids, priority, working directory and resource limits.
 */
int commandProcinfo(char **args)
{
  pid_t pid = getpid();
  bool self = true;
  if (args[1])
  {
    if (!parsePid(args[1], &pid))
    {
      fprintf(stderr, "usage: procinfo [pid]\n");
      return 1;
    }
    self = (pid == getpid());
  }

  // kill(pid, 0) sends nothing: it only checks that the process exists
  if (kill(pid, 0) == -1 && errno == ESRCH)
  {
    fprintf(stderr, "procinfo: no process with pid %d\n", (int)pid);
    return 1;
  }

  char buf[PATH_MAX + 64];
  printf("%sProcess %d%s%s\n\n", cyan(), (int)pid, self ? " (ProcShell itself)" : "", reset());

  snprintf(buf, sizeof(buf), "%d", (int)pid);
  row("PID", buf);
  if (self)
  {
    snprintf(buf, sizeof(buf), "%d", (int)getppid());
    row("Parent PID", buf);
  }

  pid_t pgid = getpgid(pid);
  pid_t sid = getsid(pid);
  snprintf(buf, sizeof(buf), "%d%s", (int)pgid, pgid == pid ? "  (group leader)" : "");
  row("Process group", buf);
  snprintf(buf, sizeof(buf), "%d%s", (int)sid, sid == pid ? "  (session leader)" : "");
  row("Session", buf);

  pid_t foreground = tcgetpgrp(STDIN_FILENO);
  row("Has terminal", foreground == pgid ? "yes (foreground group)" : "no (background)");

  errno = 0;
  int nice = getpriority(PRIO_PROCESS, (id_t)pid);
  if (errno == 0)
  {
    snprintf(buf, sizeof(buf), "%d  (-20 = highest, 19 = lowest)", nice);
    row("Nice value", buf);
  }

  if (!self)
    return 0;

  struct passwd *pw = getpwuid(getuid());
  snprintf(buf, sizeof(buf), "%s (uid %d, gid %d)", pw ? pw->pw_name : "?", (int)getuid(), (int)getgid());
  row("User", buf);

  char *cwd = getcwd(NULL, 0);
  row("Working dir", cwd ? cwd : "?");
  free(cwd);

  mode_t mask = umask(0);
  umask(mask);
  snprintf(buf, sizeof(buf), "%04o", (unsigned)mask);
  row("umask", buf);

  printf("\n%sResource limits%s (getrlimit)\n\n", cyan(), reset());
  limitToText(RLIMIT_NOFILE, buf, sizeof(buf), false);
  row("Open files", buf);
  limitToText(RLIMIT_STACK, buf, sizeof(buf), true);
  row("Stack size", buf);
  limitToText(RLIMIT_CPU, buf, sizeof(buf), false);
  row("CPU seconds", buf);
  limitToText(RLIMIT_NPROC, buf, sizeof(buf), false);
  row("Processes", buf);
  limitToText(RLIMIT_FSIZE, buf, sizeof(buf), true);
  row("File size", buf);
  return 0;
}

/* ---------- signal ---------- */

typedef struct
{
  const char *name;
  int number;
  const char *meaning;
} SignalName;

static const SignalName signalNames[] = {
    {"hup", SIGHUP, "hang up"},
    {"int", SIGINT, "interrupt (Ctrl+C)"},
    {"quit", SIGQUIT, "quit with core dump"},
    {"kill", SIGKILL, "kill now (cannot be caught)"},
    {"usr1", SIGUSR1, "user-defined 1"},
    {"usr2", SIGUSR2, "user-defined 2"},
    {"term", SIGTERM, "polite terminate"},
    {"stop", SIGSTOP, "pause (cannot be caught)"},
    {"tstp", SIGTSTP, "pause from terminal (Ctrl+Z)"},
    {"cont", SIGCONT, "resume a paused process"},
};
#define SIGNAL_NAME_COUNT (sizeof(signalNames) / sizeof(signalNames[0]))

/* "stop", "STOP", "SIGSTOP" or "19" -> signal number; -1 if unknown */
static int signalFromText(const char *text, const char **nameOut)
{
  const char *name = text;
  if (strncasecmp(name, "SIG", 3) == 0)
    name += 3;
  for (size_t i = 0; i < SIGNAL_NAME_COUNT; i++)
  {
    if (strcasecmp(name, signalNames[i].name) == 0)
    {
      *nameOut = signalNames[i].name;
      return signalNames[i].number;
    }
  }
  pid_t number;
  if (parsePid(text, &number) && number < 64)
  {
    *nameOut = NULL;
    for (size_t i = 0; i < SIGNAL_NAME_COUNT; i++)
      if (signalNames[i].number == number)
        *nameOut = signalNames[i].name;
    return number;
  }
  return -1;
}

int commandSignal(char **args)
{
  if (args[1] && strcmp(args[1], "-l") == 0)
  {
    printf("%-8s %-4s %s\n", "name", "no.", "meaning");
    for (size_t i = 0; i < SIGNAL_NAME_COUNT; i++)
      printf("%-8s %-4d %s\n", signalNames[i].name, signalNames[i].number, signalNames[i].meaning);
    return 0;
  }

  bool group = false;
  int a = 1;
  if (args[1] && strcmp(args[1], "-g") == 0)
  {
    group = true;
    a = 2;
  }

  pid_t target;
  if (!args[a] || !args[a + 1] || args[a + 2] || !parsePid(args[a], &target))
  {
    fprintf(stderr, "usage: signal [-g] <pid> <signal>     e.g. signal 4321 stop\n");
    fprintf(stderr, "       signal -l                      list signal names\n");
    fprintf(stderr, "  -g  send to the whole process group (e.g. a background job)\n");
    return 1;
  }

  const char *name = NULL;
  int sig = signalFromText(args[a + 1], &name);
  if (sig <= 0)
  {
    fprintf(stderr, "signal: unknown signal '%s' (signal -l lists them)\n", args[a + 1]);
    return 1;
  }

  // signal runs in a child of the shell: never let it stop or kill ProcShell itself
  pid_t shellPid = getppid();
  if (target == 1 || (!group && target == shellPid) || (group && target == getpgid(shellPid)))
  {
    fprintf(stderr, "signal: refusing to signal %s\n", target == 1 ? "init (pid 1)" : "ProcShell itself");
    return 1;
  }

  if (kill(group ? -target : target, sig) == -1)
  {
    perror("signal");
    return 1;
  }
  char upper[16] = "?";
  if (name)
  {
    size_t k = 0;
    for (; name[k] && k < sizeof(upper) - 1; k++)
      upper[k] = (char)toupper((unsigned char)name[k]);
    upper[k] = '\0';
  }
  printf("sent SIG%s (%d) to %s %d\n", upper, sig, group ? "process group" : "process", (int)target);
  if (name && name[0] && strcmp(name, "stop") == 0)
    printf("it is paused now; resume it with: signal %s%d cont\n", group ? "-g " : "", (int)target);
  return 0;
}

/* ---------- history ---------- */

int commandHistory(char **args)
{
  if (!history || history->size == 0)
  {
    printf("history is empty\n");
    return 0;
  }

  int show = history->size;
  if (args[1])
  {
    pid_t n;
    if (!parsePid(args[1], &n))
    {
      fprintf(stderr, "usage: history [n]\n");
      return 1;
    }
    show = MIN(show, (int)n);
  }

  int skip = history->size - show;
  int number = 1;
  for (ListNode *node = history->head; node; node = node->next, number++)
  {
    if (number <= skip)
      continue;
    printf("%5d  %s\n", number, node->command);
  }
  return 0;
}

/* ---------- timeit ---------- */

/*
 * timeit <command line> runs the rest of the line through the normal parser and executor,
 * then prints the totals the executor collected with wait4(): wall time, user and system
 * CPU time and peak memory of every process the command started.
 */
int commandTimeit(char **args, char ***env, char *initialDirectory)
{
  if (!args[1])
  {
    fprintf(stderr, "usage: timeit <command line>     e.g. timeit make re\n");
    return 1;
  }

  size_t len = 1;
  for (int i = 1; args[i]; i++)
    len += strlen(args[i]) + 1;
  char *line = malloc(len);
  if (!line)
    return 1;
  line[0] = '\0';
  for (int i = 1; args[i]; i++)
  {
    strcat(line, args[i]);
    if (args[i + 1])
      strcat(line, " ");
  }

  VectorToken *tokens = getTokens(line, *env);
  free(line);
  if (!tokens)
    return 1;
  Commands *commands = splitCommands(tokens);
  freeVecToken(tokens);
  if (!commands)
    return 1;

  RunStats saved = runStats;
  runStats = (RunStats){0, 0, 0, 0, 0};
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  int status = 0;
  for (size_t i = 0; i < commands->commands->size; i++)
  {
    status = executeCommand(commands->commands->data[i], env, initialDirectory);
    if (status == STATUS_INTERRUPTED)
      break;
  }
  freeCommands(commands);

  clock_gettime(CLOCK_MONOTONIC, &end);
  double wall = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
  RunStats measured = runStats;
  runStats = saved;

  const char *c = cyan(), *r = reset();
  printf("\n%s─── timeit ───────────────────────%s\n", c, r);
  printf("  real time     %9.3f s\n", wall);
  printf("  user CPU      %9.3f s\n", measured.userSeconds);
  printf("  system CPU    %9.3f s\n", measured.systemSeconds);
  printf("  peak memory   %9.1f MB\n", measured.peakMemoryMB);
  printf("  processes     %9d\n", measured.processes);
  printf("  exit status   %9d\n", status);
  printf("%s──────────────────────────────────%s\n", c, r);
  fflush(stdout);
  return status == 0 ? 0 : 1;
}
