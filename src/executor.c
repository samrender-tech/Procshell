#include <sys/resource.h>
#include <time.h>
#include "procshell.h"

/* "SIGINT" for 2, etc. - used by trace output */
static const char *signalName(int sig)
{
  switch (sig)
  {
  case SIGINT: return "SIGINT (Ctrl+C)";
  case SIGTERM: return "SIGTERM";
  case SIGKILL: return "SIGKILL";
  case SIGSEGV: return "SIGSEGV (segmentation fault)";
  case SIGPIPE: return "SIGPIPE (broken pipe)";
  case SIGABRT: return "SIGABRT";
  case SIGQUIT: return "SIGQUIT";
  default: return "signal";
  }
}

static double timevalSeconds(struct timeval tv)
{
  return tv.tv_sec + tv.tv_usec / 1e6;
}

/* ru_maxrss is in kilobytes on Linux but in bytes on macOS */
static double maxrssMegabytes(long maxrss)
{
#ifdef __APPLE__
  return maxrss / (1024.0 * 1024.0);
#else
  return maxrss / 1024.0;
#endif
}

/*
 * Runs every pipeline of a command, honouring && and ||.
 * Like bash, a pipeline whose condition fails is SKIPPED (not the whole chain):
 *   false && echo a || echo b   ->  prints b
 *   true  || echo a && echo c   ->  prints c
 * Returns the status of the last pipeline that ran.
 */
int executeAllPipelines(Command *command, char ***env, char *initialDirectory, int *laststatus)
{
  int last = 0;
  for (size_t i = 0; i < command->pipelines->size; i++)
  {
    if (i > 0)
    {
      int sep = command->pipelines->data[i - 1]->separator;
      if ((sep == AND && last != 0) || (sep == OR && last == 0))
        continue; // skip this pipeline, keep the previous status
    }

    last = executePipeline(command->pipelines->data[i], env, initialDirectory, command->isBackground);
    if (last == STATUS_INTERRUPTED) // Ctrl+C stops the rest of the chain
      break;
  }

  *laststatus = last;
  return last;
}

int executeCommand(Command *command, char ***env, char *initialDirectory)
{
  int laststatus = 0;
  if (command->isBackground)
  {
    pid_t backgroundShell = fork();
    if (backgroundShell == -1)
    {
      perror("fork");
      return STATUS_FAILURE;
    }

    if (backgroundShell == 0) // child process
    {
      setpgid(0, 0);

      // reset the signals for the child terminal
      signal(SIGCHLD, SIG_DFL);
      signal(SIGINT, SIG_DFL);

      // executes all the pipelines
      executeAllPipelines(command, env, initialDirectory, &laststatus);

      // ideally should be in parent shell but...
      printf("Background process exit: %d\n", laststatus);
      fflush(stdout);
      _exit(laststatus);
    }
    else
    {
      // make the background shell it's own process group
      setpgid(backgroundShell, backgroundShell);
      traceLog("fork", "background job: child %d in its own process group %d (shell does not wait)", backgroundShell, backgroundShell);

      printf("Process Group: %d\n", backgroundShell);
      fflush(stdout);
      return 0;
    }
  }

  // foreground process
  return executeAllPipelines(command, env, initialDirectory, &laststatus);
}

/* Give the terminal back to the shell and restore raw mode (foreground only). */
static void reclaimTerminal(bool isBackground)
{
  if (isBackground) // a background job never owns the terminal
    return;
  tcsetpgrp(STDIN_FILENO, getpgrp());
  enableRawMode();
}

int executePipeline(Pipeline *pipeline, char ***env, char *initialDirectory, bool isBackground)
{
  int pipelineComponentCount = pipeline->components->size;
  if (pipelineComponentCount == 0)
    return 0;

  int pipeCount = pipelineComponentCount - 1;
  int fds[pipeCount > 0 ? pipeCount : 1][2];
  int pids[pipelineComponentCount];
  for (int i = 0; i < pipelineComponentCount; i++)
    pids[i] = -1;

  traceLog("parse", "pipeline with %d command%s%s", pipelineComponentCount, pipelineComponentCount == 1 ? "" : "s",
           isBackground ? " (background)" : "");
  struct timespec startTime;
  clock_gettime(CLOCK_MONOTONIC, &startTime);

  // builtins that change shell state (cd, export, ...) and ai can't be part of a pipe
  PipelineComponent *first = pipeline->components->data[0];
  char *firstCommand = first->tokens->data[0]->token;
  if ((isBuiltin(firstCommand) || strcmp(firstCommand, "ai") == 0) && pipelineComponentCount > 1)
  {
    fprintf(stderr, "%s: cannot be used in a pipeline\n", firstCommand);
    return STATUS_FAILURE;
  }

  /*
   * Block SIGCHLD while we start and wait for this pipeline.
   * Otherwise the SIGCHLD handler could reap one of our children first,
   * and waitpid() below would lose its exit status.
   */
  sigset_t chldMask, prevMask;
  sigemptyset(&chldMask);
  sigaddset(&chldMask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &chldMask, &prevMask);

  // set up pipes for IPC
  for (int i = 0; i < pipeCount; i++)
  {
    if (pipe(fds[i]) == -1)
    {
      perror("pipe");
      closePipes(fds, i);
      sigprocmask(SIG_SETMASK, &prevMask, NULL);
      return STATUS_FAILURE;
    }
    traceLog("pipe", "pipe %d created: read end fd %d, write end fd %d", i + 1, fds[i][0], fds[i][1]);
  }

  int processGroup = INT_MIN;
  for (int i = 0; i < pipelineComponentCount; i++)
  {
    int status = executePipelineComponent(pipeline->components->data[i], env, fds, pipeCount, i, pids, initialDirectory, &processGroup, isBackground);
    if (i == 0 && !isBackground && processGroup != INT_MIN)
    {
      tcsetpgrp(STDIN_FILENO, processGroup); // the job's process group gets the terminal
      traceLog("terminal", "terminal given to process group %d (Ctrl+C now goes to it, not the shell)", processGroup);
    }

    if (status == -1)
    {
      // starting this pipeline failed: close pipes, kill and reap what already started
      closePipes(fds, pipeCount);
      killPids(0, i, pids);
      for (int j = 0; j < i; j++)
        if (pids[j] > 0)
          while (waitpid(pids[j], NULL, 0) == -1 && errno == EINTR)
            ;
      reclaimTerminal(isBackground);
      sigprocmask(SIG_SETMASK, &prevMask, NULL);
      return STATUS_FAILURE;
    }
  }

  // close parent pipes
  closePipes(fds, pipeCount);

  int lastStatus = 0;
  int waitedChildren = 0;
  double userSeconds = 0, systemSeconds = 0, peakMemoryMB = 0;
  // wait for child processes - the pipeline's status is the last command's status
  for (int j = 0; j < pipelineComponentCount; ++j)
  {
    if (pids[j] == -1)
      continue;

    pid_t w;
    int s;
    struct rusage usage;
    // wait4 = waitpid + the child's CPU time and memory usage
    while ((w = wait4(pids[j], &s, 0, &usage)) == -1 && errno == EINTR)
      ;
    if (w == pids[j])
    {
      const char *name = pipeline->components->data[j]->tokens->data[0]->token;
      waitedChildren++;
      userSeconds += timevalSeconds(usage.ru_utime);
      systemSeconds += timevalSeconds(usage.ru_stime);
      peakMemoryMB = MAX(peakMemoryMB, maxrssMegabytes(usage.ru_maxrss));

      if (WIFEXITED(s))
      {
        lastStatus = WEXITSTATUS(s);
        traceLog("wait", "child %d (%s) exited with status %d - reaped, no zombie left", w, name, lastStatus);
      }
      else if (WIFSIGNALED(s))
      {
        lastStatus = STATUS_SIGNAL_BASE + WTERMSIG(s);
        write(STDOUT_FILENO, "\n", 1);
        traceLog("signal", "child %d (%s) was killed by %s", w, name, signalName(WTERMSIG(s)));
      }
    }
  }

  reclaimTerminal(isBackground);
  if (!isBackground && processGroup != INT_MIN)
    traceLog("terminal", "terminal returned to the shell (process group %d)", (int)getpgrp());

  if (waitedChildren > 0)
  {
    struct timespec endTime;
    clock_gettime(CLOCK_MONOTONIC, &endTime);
    double realSeconds = (endTime.tv_sec - startTime.tv_sec) + (endTime.tv_nsec - startTime.tv_nsec) / 1e9;
    traceLog("stats", "real %.3fs | user CPU %.3fs | system CPU %.3fs | peak memory %.1f MB",
             realSeconds, userSeconds, systemSeconds, peakMemoryMB);
  }
  sigprocmask(SIG_SETMASK, &prevMask, NULL);
  return lastStatus;
}

int executePipelineComponent(PipelineComponent *pc, char ***env, int fds[][2], int pipeCount, int i, int pids[], char *initialDirectory, int *processGroup, bool isBackground)
{
  Token **tokens = pc->tokens->data;
  int tokensLen = pc->tokens->size;
  int commandCount = pipeCount + 1;

  char *infile = NULL;
  char *outfile = NULL;
  int commandEnd = tokensLen;
  // from the given pipeline component find it's infile or outfile if any
  bool isAppendOutputFile = findInOutFileAndCommandEnd(pc, &infile, &outfile, &commandEnd);
  int parentRetValue = 0;
  fflush(stdout);

  int infilefd = -1;
  int outfilefd = -1;
  if (infile)
  {
    infilefd = open(infile, O_RDONLY);
    if (infilefd == -1)
    {
      perror(infile);
      parentRetValue = -1;
      goto closeFiles;
    }
    traceLog("redirect", "opened %s as fd %d -> will become stdin (fd 0)", infile, infilefd);
  }
  if (outfile)
  {
    int fileOpenMode = isAppendOutputFile ? O_WRONLY | O_CREAT | O_APPEND : O_WRONLY | O_CREAT | O_TRUNC;
    outfilefd = open(outfile, fileOpenMode, 0666);
    if (outfilefd == -1)
    {
      perror(outfile);
      parentRetValue = -1;
      goto closeFiles;
    }
    traceLog("redirect", "opened %s as fd %d (%s) -> will become stdout (fd 1)", outfile, outfilefd,
             isAppendOutputFile ? "append" : "overwrite");
  }

  // get the args in the pipeline component to give to execve
  char **args = malloc(sizeof(char *) * (commandEnd + 1));
  if (!args)
  {
    parentRetValue = -1;
    goto closeFiles;
  }

  for (int k = 0; k < commandEnd; k++)
  {
    args[k] = tokens[k]->token;
  }
  args[commandEnd] = NULL;

  // if it's the only builtin then execute it. no need to fork
  if (i == 0 && isBuiltin(args[0]))
  {
    traceLog("builtin", "'%s' runs inside the shell itself - no fork, because it changes the shell's own state", args[0]);
    parentRetValue = handleBuiltin(args, env, initialDirectory) == 0 ? 0 : -1;
    free(args);
    goto closeFiles;
  }

  pid_t pid = fork();
  if (pid == -1)
  {
    free(args);
    perror("fork");
    parentRetValue = -1;
    goto closeFiles;
  }
  else if (pid == 0)
  {
    if (*processGroup == INT_MIN) // first process in the pipeline
      setpgid(0, 0);
    else
      setpgid(0, *processGroup); // following processes added to previous group

    if (!isBackground) // background processes won't have access to terminal
      disableRawMode();
    signal(SIGINT, SIG_DFL);

    int exitError = 0;

    // if infile or outfile connect them
    if (infile && dup2(infilefd, STDIN_FILENO) == -1)
    {
      exitError = 1;
      goto exitChild;
    }
    if (outfile && dup2(outfilefd, STDOUT_FILENO) == -1)
    {
      exitError = 1;
      goto exitChild;
    }

    // connect rest of the pipes (an explicit redirect wins over the pipe)
    if (i != 0 && !infile && dup2(fds[i - 1][0], STDIN_FILENO) == -1)
    {
      exitError = 1;
      goto exitChild;
    }
    if (i != commandCount - 1 && !outfile && dup2(fds[i][1], STDOUT_FILENO) == -1)
    {
      exitError = 1;
      goto exitChild;
    }

  exitChild:
    // close all pipes as necessary ones are already duped above
    closePipes(fds, pipeCount);

    // close the files if opened
    if (infile)
      close(infilefd);
    if (outfile)
      close(outfilefd);

    if (exitError)
    {
      free(args);
      _exit(exitError);
    }

    // handle builtins
    if (isBuiltin(args[0]))
    {
      int status = handleBuiltin(args, env, initialDirectory);
      free(args);
      _exit(status == 0 ? 0 : STATUS_FAILURE);
    }

    // handle my implemented builtins
    if (isMyImplementedBuiltin(args[0]))
    {
      traceLog("builtin", "child %d runs Procshell's own '%s' (our C code, no exec)", (int)getpid(), args[0]);
      int status = handleMyImplementedBuiltin(args, env, initialDirectory);
      free(args);
      _exit(status == 0 ? 0 : STATUS_FAILURE);
    }

    // external program: a name with '/' is a path (./a.out, /bin/ls, ../x), otherwise search PATH
    char *path = NULL;
    if (myStrchr(args[0], '/'))
    {
      if (access(args[0], X_OK) == 0)
        path = myStrdup(args[0]);
    }
    else
    {
      path = getFullPathOfWhich(args[0], *env);
    }

    if (!path) // command not found
    {
      fprintf(stderr, "%s: command not found\n", args[0]);
      _exit(127); // command not found code
    }

    traceLog("exec", "child %d replaces itself with %s", (int)getpid(), path);
    // the new program must start with a normal signal mask
    sigprocmask(SIG_SETMASK, &shellOrigMask, NULL);
    execve(path, args, *env); // if it returns then that's an error
    perror(args[0]);

    free(path);
    free(args);
    _exit(126); // command found but couldn't execute
  }
  else // parent
  {
    // from the parent side also set up the process group to avoid race condition
    if (*processGroup == INT_MIN)
    {
      setpgid(pid, pid);
      *processGroup = pid;
    }
    else
    {
      setpgid(pid, *processGroup);
    }
    // give the parent the process id of the child
    pids[i] = pid;
    traceLog("fork", "child %d created for '%s' -> process group %d", pid, args[0], *processGroup);
    free(args);
  closeFiles:
    // parent closing all infiles/outfiles it opened
    if (infilefd != -1)
      close(infilefd);
    if (outfilefd != -1)
      close(outfilefd);

    return parentRetValue;
  }
}
