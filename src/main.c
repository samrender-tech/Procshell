#include <signal.h>
#include <libgen.h>
#include <sys/stat.h>
#include "procshell.h"

volatile int termCols;                       // tells about the columns in the terminal (width of column)
volatile sig_atomic_t whichSignal = INT_MIN; // initiate it with invalid signal
struct termios orig_termios;
ForgettingDoublyLinkedList *history = NULL;
sigset_t shellOrigMask;        // signal mask at startup - children get this back before exec
char *procshellHome = NULL;    // folder that contains the procshell binary (and ai/interface.py)
char *procshellDataDir = NULL; // ~/.procshell - AI temp/context files
time_t procshellStartTime = 0; // when the shell started

void shellLoop(char **env);

/* Welcome banner shown once when Procshell starts. */
static void printBanner(void)
{
  printf("\033[1;36m");
  printf("  ____                     _          _ _ \n");
  printf(" |  _ \\ _ __ ___   ___ ___| |__   ___| | |\n");
  printf(" | |_) | '__/ _ \\ / __/ __| '_ \\ / _ \\ | |\n");
  printf(" |  __/| | | (_) | (__\\__ \\ | | |  __/ | |\n");
  printf(" |_|   |_|  \\___/ \\___|___/_| |_|\\___|_|_|\n");
  printf("\033[0m\n");
  printf(" A Unix shell built from scratch in C, with AI assistance\n\n");
  printf(" by \033[1mSamrender Singh Saini\033[0m & \033[1mVartika Chaudhary\033[0m\n\n");
  printf(" Type \033[1mhelp\033[0m for commands, \033[1mai <request>\033[0m to ask the AI, \033[1mexit\033[0m to quit.\n\n");
  fflush(stdout);
}

/* Find the folder the procshell binary lives in, so ai/interface.py is found from any directory. */
static char *findHomeDirectory(const char *argv0)
{
  char resolved[PATH_MAX];
  if (strchr(argv0, '/') && realpath(argv0, resolved))
  {
    return strdup(dirname(resolved));
  }
  return getcwd(NULL, 0); // fallback: the folder procshell was started from
}

/* ~/.procshell holds the AI context files instead of cluttering the current folder. */
static char *createDataDirectory(void)
{
  const char *home = getenv("HOME");
  char path[PATH_MAX];
  if (home)
    snprintf(path, sizeof(path), "%s/.procshell", home);
  else
    snprintf(path, sizeof(path), "/tmp/procshell-%d", (int)getuid());

  if (mkdir(path, 0700) == -1 && errno != EEXIST)
  {
    perror("procshell: could not create data directory");
    return NULL;
  }
  return strdup(path);
}

int main(int argc, char const *argv[], char *envp[])
{
  (void)argc;
  procshellStartTime = time(NULL);

  sigprocmask(SIG_SETMASK, NULL, &shellOrigMask); // remember the normal signal mask

  struct sigaction sa;
  sa.sa_handler = handleSignal;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);

  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGCHLD, &sa, NULL);
  sigaction(SIGWINCH, &sa, NULL);

  /**
   * Whenever a child terminal is created by forking this process it inherits the stdout of this process
   * In that case whenever the command like ls & tries to write in the child terminal it inherently is trying
   * to write in the main/parent terminal which will be blocked until we ignore the SIGTTOU
   * SIGTTOU - Signal Terminal Output for Background Process.
   */
  signal(SIGTTOU, SIG_IGN); // so background writes don’t stop

  /**
   * Ctrl+Z (SIGTSTP): Procshell has no fg/bg yet, so a stopped job could never be resumed
   * and the shell would wait for it forever. Ignore it in the shell and its children.
   */
  signal(SIGTSTP, SIG_IGN);

  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
    termCols = w.ws_col;
  else
    termCols = 80; // not a real terminal: avoid dividing by zero in the line editor

  procshellHome = findHomeDirectory(argv[0]);
  procshellDataDir = createDataDirectory();

  shellLoop(envp);
  return 0;
}

void shellLoop(char **envp)
{
  history = createFDLL(100);
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
  {
    fprintf(stderr, "procshell: stdin is not a terminal\n");
    exit(EXIT_FAILURE);
  }

  atexit(disableRawMode);
  enableRawMode();

  char *input = NULL; // input string
  char *initialDirectory = getcwd(NULL, 0);
  char **env = NULL;
  if (cloneEnv(envp, &env) == -1)
  {
    fprintf(stderr, "environment duplication failed\n");
    exit(EXIT_FAILURE);
  }
  char *userName = myGetenv("LOGNAME", env);

  printBanner();

  int status = 0;
  while (1)
  {
    if (status == 0)
      printShellStart(env, userName);
    status = getInputString(history, &input);
    if (input == NULL || status != 0)
      continue;

    VectorToken *tokenVec = getTokens(input, env);
    if (tokenVec == NULL)
    {
      fprintf(stderr, "Lexical phase failed\n");
      free(input);
      input = NULL;
      continue;
    }

    if (tokenVec->size > 0)
    {
      if (history->size > 0)
      {
        if (myStrcmp(input, history->tail->command) != 0)
          insertInFDLL(history, input);
      }
      else
      {
        insertInFDLL(history, input);
      }
    }
    free(input); // free the input string
    input = NULL;

    Commands *allCommands = splitCommands(tokenVec);
    if (allCommands == NULL)
    {
      freeVecToken(tokenVec);
      fprintf(stderr, "Parsing failed\n");
      continue;
    }
    freeVecToken(tokenVec);

    for (size_t i = 0; i < allCommands->commands->size; i++)
    {
      Command *command = allCommands->commands->data[i];
      int commandStatus = executeCommand(command, &env, initialDirectory);
      if (commandStatus == STATUS_INTERRUPTED)
        break; // Ctrl+C stops the rest of the line (like bash); a normal failure does not
    }
    freeCommands(allCommands);
  }

  freeFDLL(history);
  free(userName);
  freeEnv(env);
}
