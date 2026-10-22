#include <poll.h>
#include <sys/stat.h>
#include "procshell.h"
extern ForgettingDoublyLinkedList *history;

// AI context files, stored in ~/.procshell (see procshellDataDir)
#define AI_OUTPUT_FILE "aiOutput.txt"
#define AI_ERROR_FILE "aiError.txt"
#define AI_HISTORY_FILE "aiHistory.txt"
#define AI_PLATFORM_FILE "aiPlatform.txt"
#define AI_SCRIPT_FILE "aiCommands.sh"

int commandCd(char **args, char *initialDirectory)
{
  (void)initialDirectory;
  const char *target = args[1];
  if (target == NULL) // plain "cd" goes home, like bash
  {
    target = getenv("HOME");
    if (target == NULL)
    {
      fprintf(stderr, "cd: HOME not set\n");
      return -1;
    }
  }

  if (chdir(target) != 0)
  {
    perror("cd");
    return -1;
  }

  return 0;
}

int commandPwd()
{
  char *cwd = getcwd(NULL, 0);
  if (cwd == NULL)
  {
    return -1;
  }
  fprintf(stdout, "%s\n", cwd);
  free(cwd);
  return 0;
}

int commandEcho(char **args, char **env)
{
  (void)env;
  int printNewLine = true;
  int start = 1;
  if (args[1] && myStrcmp(args[1], "-n") == 0) // handling -n flag for no new line character
  {
    start = 2;
    printNewLine = false;
  }

  for (int i = start; args[i]; i++) // parse all the arguments for echo
  {
    fprintf(stdout, "%s", args[i]);
    if (args[i + 1])
    {
      fprintf(stdout, " ");
    }
  }

  if (printNewLine)
    fprintf(stdout, "\n");

  return 0;
}

int commandSource(char **args, char ***env, char *initialDirectory)
{
  if (args[1] == NULL)
  {
    fprintf(stderr, "source: filename required\n");
    return -1;
  }

  FILE *f = fopen(args[1], "r");
  if (!f)
  {
    perror("source");
    return -1;
  }

  char input[1024];
  while (fgets(input, sizeof(input), f))
  {
    input[strcspn(input, "\r\n")] = '\0'; // fgets keeps the newline
    if (input[0] == '\0' || input[0] == '#') // skip blank lines and comments
      continue;

    VectorToken *tokenVec = getTokens(input, *env);
    if (tokenVec == NULL)
    {
      fprintf(stderr, "Lexical phase failed\n");
      continue;
    }
    Commands *allCommands = splitCommands(tokenVec);
    if (allCommands == NULL)
    {
      freeVecToken(tokenVec);
      continue;
    }
    freeVecToken(tokenVec);

    bool interrupted = false;
    for (size_t i = 0; i < allCommands->commands->size; i++)
    {
      Command *command = allCommands->commands->data[i];
      if (executeCommand(command, env, initialDirectory) == STATUS_INTERRUPTED)
      {
        interrupted = true; // Ctrl+C stops the whole script
        break;
      }
    }
    freeCommands(allCommands);
    if (interrupted)
      break;
  }

  fclose(f);
  return 0;
}

int commandEnv(char **env)
{
  fprintf(stdout, "%-40s | %s\n", "Enviornment Variable", "Value");
  fprintf(stdout, "-------------------------------------------------------------------------------------------\n");
  if (env == NULL)
    return -1;

  for (int i = 0; env[i]; i++)
  {
    int count = 0;
    for (int j = 0; env[i][j]; j++)
    {
      if (env[i][j] == '=')
        break;
      count++;
    }

    char *buff = (char *)malloc(sizeof(char) * (count + 1));
    if (buff == NULL)
    {
      return -1;
    }
    strncpy(buff, env[i], count); // get value of the enviornment variable to print
    buff[count] = '\0';

    fprintf(stdout, "%-40s | %s\n", buff, env[i] + count + 1); // formatted print key and value
    free(buff);
  }

  return 0;
}

/* Builds "<data dir>/<name>" - caller frees. */
static char *aiFilePath(const char *name)
{
  const char *dir = procshellDataDir ? procshellDataDir : ".";
  size_t len = strlen(dir) + 1 + strlen(name) + 1;
  char *path = malloc(len);
  if (path)
    snprintf(path, len, "%s/%s", dir, name);
  return path;
}

/* Asks "[y/N]" and reads the whole line. Default (just Enter) is NO - nothing runs by accident. */
static bool askToRunCommands(void)
{
  printf("Do you want to execute the commands? [y/N]: ");
  fflush(stdout);

  char *line = NULL;
  size_t n = 0;
  ssize_t nRead = getline(&line, &n, stdin);
  bool yes = nRead > 0 && (line[0] == 'y' || line[0] == 'Y');
  free(line);
  return yes;
}

/*
 * Runs the AI's commands with bash. Both stdout and stderr come back to us through pipes:
 * stdout is shown live and counted (so we know if the commands printed anything),
 * stderr is shown live and also saved, so the next attempt can show the AI what went wrong.
 * Returns bash's exit code (0 = all commands succeeded), or -1 on failure to start.
 */
static int runAICommands(AICommands *commands, const char *scriptPath, const char *errorPath, long *stdoutBytes)
{
  *stdoutBytes = 0;
  FILE *fp = fopen(scriptPath, "w");
  if (!fp)
  {
    perror("ai: could not write script");
    return -1;
  }
  fprintf(fp, "set -e\n"); // stop at the first failing command, so the failure is reported
  for (int i = 0; i < commands->commandsCount; i++)
    fprintf(fp, "%s\n", commands->commands[i]);
  fclose(fp);

  int outPipe[2], errPipe[2];
  if (pipe(outPipe) == -1)
  {
    perror("pipe");
    return -1;
  }
  if (pipe(errPipe) == -1)
  {
    perror("pipe");
    close(outPipe[0]);
    close(outPipe[1]);
    return -1;
  }

  pid_t pid = fork();
  if (pid == -1)
  {
    perror("fork");
    close(outPipe[0]);
    close(outPipe[1]);
    close(errPipe[0]);
    close(errPipe[1]);
    return -1;
  }
  if (pid == 0) // child: bash with stdout and stderr going into our pipes
  {
    close(outPipe[0]);
    close(errPipe[0]);
    dup2(outPipe[1], STDOUT_FILENO);
    dup2(errPipe[1], STDERR_FILENO);
    close(outPipe[1]);
    close(errPipe[1]);
    sigprocmask(SIG_SETMASK, &shellOrigMask, NULL);
    char *bashArgs[] = {"bash", (char *)scriptPath, NULL};
    execvp("bash", bashArgs);
    perror("bash");
    _exit(127);
  }

  // parent: read both pipes until bash closes them (poll waits for whichever has data)
  close(outPipe[1]);
  close(errPipe[1]);
  FILE *errorFile = fopen(errorPath, "w");
  struct pollfd fds[2] = {{outPipe[0], POLLIN, 0}, {errPipe[0], POLLIN, 0}};
  int open = 2;
  char buf[4096];
  while (open > 0)
  {
    if (poll(fds, 2, -1) == -1)
    {
      if (errno == EINTR)
        continue;
      break;
    }
    for (int k = 0; k < 2; k++)
    {
      if (fds[k].fd < 0 || !(fds[k].revents & (POLLIN | POLLHUP | POLLERR)))
        continue;
      ssize_t r = read(fds[k].fd, buf, sizeof(buf));
      if (r > 0)
      {
        if (k == 0)
        {
          write(STDOUT_FILENO, buf, r);
          *stdoutBytes += r;
        }
        else
        {
          write(STDERR_FILENO, buf, r);
          if (errorFile)
            fwrite(buf, 1, r, errorFile);
        }
      }
      else if (r == 0 || errno != EINTR) // EOF on this pipe
      {
        close(fds[k].fd);
        fds[k].fd = -1;
        open--;
      }
    }
  }
  if (errorFile)
    fclose(errorFile);

  int status;
  while (waitpid(pid, &status, 0) == -1)
  {
    if (errno != EINTR)
      return -1;
  }
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return STATUS_SIGNAL_BASE + WTERMSIG(status);
}

/* Prints up to maxLines lines of a small text file, framed, so the user sees what was written. */
static void showFileContents(const char *path, int maxLines)
{
  struct stat st;
  if (stat(path, &st) == -1 || !S_ISREG(st.st_mode) || st.st_size > 64 * 1024)
    return;
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  printf("\n\033[1;36m%s\033[0m\n", path);
  char line[1024];
  int n = 0;
  while (fgets(line, sizeof(line), f))
  {
    if (++n > maxLines)
    {
      printf("  \033[36m│\033[0m ... (more lines, see: show %s)\n", path);
      break;
    }
    line[strcspn(line, "\n")] = '\0';
    printf("  \033[36m│\033[0m %s\n", line);
  }
  if (n == 0)
    printf("  \033[36m│\033[0m (empty file)\n");
  fclose(f);
}

/*
 * When the AI's commands printed nothing, show their result anyway:
 * every file they wrote with "> file" or ">> file" (heredoc bodies are skipped),
 * and every folder they made with mkdir.
 */
static void showSilentResult(AICommands *commands)
{
  bool shownSomething = false;
  char heredocEnd[64] = "";

  for (int i = 0; i < commands->commandsCount; i++)
  {
    const char *line = commands->commands[i];

    if (heredocEnd[0]) // inside a heredoc body: only look for its end marker
    {
      if (strcmp(line, heredocEnd) == 0)
        heredocEnd[0] = '\0';
      continue;
    }

    // start of a heredoc: remember its end marker, e.g. << 'EOF'  or  <<EOF
    const char *h = strstr(line, "<<");
    if (h)
    {
      h += 2;
      while (*h == ' ' || *h == '-' || *h == '\'' || *h == '"')
        h++;
      size_t k = 0;
      while (h[k] && h[k] != '\'' && h[k] != '"' && h[k] != ' ' && k < sizeof(heredocEnd) - 1)
      {
        heredocEnd[k] = h[k];
        k++;
      }
      heredocEnd[k] = '\0';
    }

    // a file written with > or >> (not 2> and not >&)
    for (const char *p = line; (p = strchr(p, '>')) != NULL; p++)
    {
      if ((p > line && (p[-1] == '2' || p[-1] == '&')) || p[1] == '&')
        continue;
      const char *q = p + 1;
      if (*q == '>')
        q++;
      while (*q == ' ')
        q++;
      char target[PATH_MAX];
      size_t t = 0;
      while (*q && *q != ' ' && *q != ';' && *q != '&' && *q != '|' && t < sizeof(target) - 1)
      {
        if (*q != '\'' && *q != '"')
          target[t++] = *q;
        q++;
      }
      target[t] = '\0';
      if (t > 0 && strcmp(target, "/dev/null") != 0)
      {
        showFileContents(target, 40);
        shownSomething = true;
      }
      break;
    }

    // folders made with mkdir
    if (strncmp(line, "mkdir ", 6) == 0)
    {
      printf("\n\033[1;36mcreated folder(s):\033[0m %s\n", line + 6);
      shownSomething = true;
    }
  }

  if (!shownSomething)
    printf("\n(the commands ran but printed nothing)\n");
}

/* Runs ai/interface.py once. Returns 0 if it produced an output file. */
static int callAIModel(const char *aiQuery)
{
  size_t len = strlen(procshellHome) + strlen("/ai/interface.py") + 1;
  char *interfacePath = malloc(len);
  if (!interfacePath)
    return -1;
  snprintf(interfacePath, len, "%s/ai/interface.py", procshellHome);

  pid_t pid = fork();
  if (pid == -1)
  {
    perror("fork");
    free(interfacePath);
    return -1;
  }
  if (pid == 0)
  {
    sigprocmask(SIG_SETMASK, &shellOrigMask, NULL);
    char *pythonArgs[] = {"python3", interfacePath, "--data-dir", procshellDataDir, (char *)aiQuery, NULL};
    execvp(pythonArgs[0], pythonArgs);
    perror("python3");
    _exit(127);
  }
  free(interfacePath);

  int status;
  while (waitpid(pid, &status, 0) == -1)
  {
    if (errno != EINTR)
      return -1;
  }
  if (WIFSIGNALED(status))
  {
    fprintf(stderr, "ai: stopped by signal %d\n", WTERMSIG(status));
    return -1;
  }
  return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* Appends "; Clarification: <answer>" to the query. Returns the (possibly moved) query or NULL. */
static char *appendClarification(char *aiQuery, const char *answer)
{
  const char *label = "; Clarification: ";
  size_t need = strlen(aiQuery) + strlen(label) + strlen(answer) + 1;
  char *grown = realloc(aiQuery, need);
  if (!grown)
    return NULL;
  strcat(grown, label);
  strcat(grown, answer);
  return grown;
}

#define TEXT_OR_EMPTY(s) ((s) ? (s) : "")

int commandAI(char **args)
{
  if (!procshellDataDir || !procshellHome)
  {
    fprintf(stderr, "ai: procshell data directory is not available\n");
    return -1;
  }
  if (args[1] == NULL)
  {
    fprintf(stderr, "usage: ai <what you want to do>\n");
    return -1;
  }

  printf("\033[31mAI never changes the state of current shell\033[0m\n\n");

  char *outputPath = aiFilePath(AI_OUTPUT_FILE);
  char *errorPath = aiFilePath(AI_ERROR_FILE);
  char *historyPath = aiFilePath(AI_HISTORY_FILE);
  char *platformPath = aiFilePath(AI_PLATFORM_FILE);
  char *scriptPath = aiFilePath(AI_SCRIPT_FILE);
  char *aiQuery = NULL;
  char *questionAnswer = NULL; // if any question asked by ai would be answered as this
  AICommands *commands = NULL; // containers for storing ai output
  AIQuestions *questions = NULL;
  int exitCode = -1;

  if (!outputPath || !errorPath || !historyPath || !platformPath || !scriptPath)
    goto exitAI;

  if (writeNHistoryInfoToFile(historyPath, AI_HISTORY_COUNT, history) == -1 ||
      writePlatformInfoToFile(platformPath) == -1 ||
      writeErrorInfoToFile(errorPath, "") == -1) // start with no errors
    goto exitAI;

  // join all words after "ai" into one query, sized exactly
  size_t queryLen = 1;
  for (int i = 1; args[i]; i++)
    queryLen += strlen(args[i]) + 1;
  aiQuery = malloc(queryLen);
  if (!aiQuery)
    goto exitAI;
  aiQuery[0] = '\0';
  for (int i = 1; args[i]; i++)
  {
    strcat(aiQuery, args[i]);
    if (args[i + 1])
      strcat(aiQuery, " ");
  }

  for (int attempt = 0; attempt < MAX_AI_ATTEMPTS; attempt++)
  {
    if (questionAnswer)
    {
      char *grown = appendClarification(aiQuery, questionAnswer);
      free(questionAnswer);
      questionAnswer = NULL;
      if (!grown)
        goto exitAI;
      aiQuery = grown;
    }

    if (callAIModel(aiQuery) == -1)
      goto exitAI; // interface.py already printed why

    freeAICommands(commands);
    freeAIQuestions(questions);
    commands = NULL;
    questions = NULL;
    if (parseAI(&commands, &questions, outputPath) == -1)
    {
      fprintf(stderr, "ai: could not understand the model's reply\n");
      goto exitAI;
    }

    printf("-----Attempt %d/%d------\n", attempt + 1, MAX_AI_ATTEMPTS);
    if (commands)
    {
      if (commands->commandsCount > 0)
      {
        // show the script as one block (a heredoc spans several lines, so no per-line bullets)
        printf("\nCommands to run:\n");
        for (int i = 0; i < commands->commandsCount; i++)
          printf("  \033[36m│\033[0m %s\n", commands->commands[i]);
      }
      printf("\n\033[32mExplanation: %s\033[0m\n", TEXT_OR_EMPTY(commands->explanation));
      if (commands->warning && commands->warning[0])
        printf("\n\033[31mWarning: %s\033[0m\n", commands->warning);
      printf("\n");

      if (commands->commandsCount == 0) // it was just an explanation
      {
        exitCode = 0;
        goto exitAI;
      }

      if (!askToRunCommands())
      {
        printf("Cancelled. Nothing was run.\n");
        exitCode = 0;
        goto exitAI;
      }
      printf("\n");

      long printed = 0;
      int code = runAICommands(commands, scriptPath, errorPath, &printed);
      if (code == 0) // all commands succeeded
      {
        if (printed == 0) // nothing on screen yet: show what the commands produced
          showSilentResult(commands);
        printf("\n\033[32m✓ Done - all commands finished successfully.\033[0m\n");
        exitCode = 0;
        goto exitAI;
      }
      if (code == -1)
        goto exitAI;
      printf("\n\033[33mCommands failed (exit %d) - asking the AI to fix them...\033[0m\n\n", code);
      continue; // retry with the error file filled in
    }
    else if (questions)
    {
      printf("\nAnswer these questions to get accurate results: \n");
      for (int i = 0; i < questions->questionsCount; i++)
        printf("- %s\n", questions->questions[i]);
      printf("\n\033[32mExplanation: %s\033[0m\n", TEXT_OR_EMPTY(questions->explanation));

      printf("Answer: \n");
      size_t n = 0;
      ssize_t nRead = getline(&questionAnswer, &n, stdin);
      if (nRead <= 0)
        goto exitAI;
      if (questionAnswer[nRead - 1] == '\n')
        questionAnswer[nRead - 1] = '\0';
      continue;
    }
    else
    {
      goto exitAI;
    }
  }
  fprintf(stderr, "ai: gave up after %d attempts\n", MAX_AI_ATTEMPTS);

exitAI:
  free(aiQuery);
  free(questionAnswer);
  freeAICommands(commands);
  freeAIQuestions(questions);
  free(outputPath);
  free(errorPath);
  free(historyPath);
  free(platformPath);
  free(scriptPath);
  return exitCode;
}

int commandWhich(char **args, char **env)
{
  if (args[1] == NULL)
  {
    fprintf(stderr, "which: expected a filename: which [file]\n");
    return -1;
  }

  // if one of these then it's a builtin command
  const char *builtIns[] = {"cd", "export", "unset", "source", "exit", "quit", "pwd", "echo", "env", "which", "ai",
                            "help", "display", "show", "makedir", "makefile", "remove", "copy", "move",
                            "fileinfo", "where", "goto", "trace", "tree", "search", "diskuse", "count", "peek",
                            "chmode", "makelink", "sysinfo", "procinfo", "signal", "timeit", "history"};

  for (size_t i = 0; i < sizeof(builtIns) / sizeof(builtIns[0]); i++)
  {
    if (myStrcmp(builtIns[i], args[1]) == 0)
    {
      fprintf(stdout, "%s: shell built-in command\n", builtIns[i]);
      return 0;
    }
  }

  // look through everywhere in env PATH where the command is
  char *fullpath = getFullPathOfWhich(args[1], env);
  if (fullpath == NULL)
  {
    fprintf(stdout, "%s: command not found\n", args[1]);
    return -1;
  }

  fprintf(stdout, "%s\n", fullpath);
  free(fullpath);
  return 0;
}

char *getFullPathOfWhich(char *command, char **env)
{
  // Locate the path enviornment variable
  char *pathEnv = myGetenv("PATH", env);
  if (!pathEnv)
  {
    return NULL;
  }

  // split the PATH by separator
  char *tok = strtok(pathEnv, SEP);

  while (tok)
  {
    size_t len = myStrlen(tok) + 1 + myStrlen(command) + 1; // +1 for / and +1 for '\0'
    char *buff = (char *)malloc(sizeof(char) * len);
    if (!buff)
    {
      free(pathEnv);
      return NULL;
    }

    if (tok[myStrlen(tok) - 1] != *PATH_SEPARATOR) // does this PATH entry already end in '/'?
      snprintf(buff, len, "%s%s%s", tok, PATH_SEPARATOR, command);
    else
      snprintf(buff, len, "%s%s", tok, command);

    // find the binary file if it exists or not in the given path
    if (access(buff, X_OK) == 0)
    {
      free(pathEnv);
      return buff; // caller must free
    }

    free(buff);
    tok = strtok(NULL, SEP);
  }

  free(pathEnv);
  return NULL;
}

int commandExport(char **args, char ***envReference)
{
  char **env = *envReference;
  if (!args[1])
  {
    fprintf(stderr, "export: expected VAR=value\n");
    return -1;
  }

  // count and allocate new enviornment variable string
  int envCount = 0;
  while (env[envCount])
    envCount++;
  char **newEnv = (char **)malloc(sizeof(char *) * (envCount + 2)); // 1 for null 1 for new env
  if (newEnv == NULL)
  {
    return -1;
  }

  // get the new enviornment variable - variable name
  char *str = args[1];
  int varnamelen = 0;

  while (*str && *str != '=')
  {
    str++;
    varnamelen++;
  }

  // making sure new enviornment variable has a value associated with it
  if (*str != '=' || varnamelen == 0)
  {
    fprintf(stderr, "export: expected VAR=value\n");
    free(newEnv);
    return -1;
  }

  // actually copy the varname
  char *varname = (char *)malloc(sizeof(char) * (varnamelen + 1));
  if (varname == NULL)
  {
    free(newEnv);
    return -1;
  }
  strncpy(varname, args[1], varnamelen);
  varname[varnamelen] = '\0';
  str++; // skip '='

  int k = 0; // pointer for newEnv variable
  size_t len;
  char *curr;
  newEnv[k] = NULL;

  for (int i = 0; env[i]; i++)
  {
    // copy the others into newEnv
    if (!(strncmp(env[i], varname, varnamelen) == 0 && env[i][varnamelen] == '=')) // case-sensitive
    {
      len = myStrlen(env[i]);
      curr = (char *)malloc(sizeof(char) * (len + 1));
      if (!curr) // malloc failed and delete everything
      {
        free(varname);
        freeEnv(newEnv);
        return -1;
      }
      myStrcpy(curr, env[i]);
      newEnv[k++] = curr;
      newEnv[k] = NULL;
    }
  }

  // copy the new enviornment variable
  len = myStrlen(args[1]);                         // 1 for '='
  curr = (char *)malloc(sizeof(char) * (len + 1)); // 1 for \0
  if (!curr)
  {
    free(varname);
    freeEnv(newEnv);
    return -1;
  }

  snprintf(curr, len + 1, "%s", args[1]);
  newEnv[k++] = curr;
  newEnv[k] = NULL;

  // free the memory
  free(varname);
  freeEnv(env);
  *envReference = newEnv;

  return 0;
}

int commandUnset(char **args, char ***envReference)
{
  char **env = *envReference;
  if (args == NULL || args[1] == NULL || env == NULL)
  {
    fprintf(stderr, "expected argument unset [env]");
    return -1;
  }

  // count env
  size_t envCount = 0;
  while (env[envCount])
    envCount++;

  if (envCount == 0)
  {
    return 0; // nothing to unset
  }

  // in case variable not removed - makes sure there's always space for NULL
  char **newEnv = malloc(sizeof(char *) * (envCount + 1)); // removing 1 and a adding null terminator
  if (!newEnv)
  {
    return -1;
  }
  newEnv[0] = NULL;

  bool found = false;

  char *varName = args[1];
  size_t varLen = myStrlen(varName);
  int k = 0;

  for (int i = 0; env[i]; i++)
  {
    if (!found && (strncmp(env[i], varName, varLen) == 0) && (env[i][varLen] == '=')) // case-sensitive
    {
      // found the enviornment variable
      found = true;
      continue;
    }

    size_t len = myStrlen(env[i]);
    newEnv[k] = (char *)malloc(sizeof(char) * (len + 1)); // +1 for '\0'
    if (!newEnv[k])
    {
      freeEnv(newEnv);
      return -1;
    }

    strcpy(newEnv[k], env[i]);
    k++;
    newEnv[k] = NULL;
  }

  if (!found)
  {
    freeEnv(newEnv);
    return 0;
  }

  freeEnv(env);
  *envReference = newEnv;
  return 0;
}

int cloneEnv(char **env, char ***newEnvReference)
{
  if (env == NULL)
  {
    *newEnvReference = NULL;
    return -1;
  }

  // count and allocate new enviornment variable string
  int envCount = 0;
  while (env[envCount])
  {
    envCount++;
  }
  char **newEnv = (char **)malloc(sizeof(char *) * (envCount + 1)); // 1 for null
  if (newEnv == NULL)
  {
    *newEnvReference = NULL;
    return -1;
  }
  int k = 0; // pointer for newEnv variable
  size_t len;
  char *curr;
  newEnv[0] = NULL;

  for (int i = 0; env[i]; i++)
  {
    // clone everything
    len = myStrlen(env[i]);
    curr = (char *)malloc(sizeof(char) * (len + 1));
    if (!curr)
    {
      freeEnv(newEnv);
      *newEnvReference = NULL;
      return -1;
    }
    myStrcpy(curr, env[i]);
    newEnv[k++] = curr;
    newEnv[k] = NULL;
  }
  *newEnvReference = newEnv;
  return 0;
}

void freeEnv(char **env)
{
  if (env == NULL)
    return;

  for (int i = 0; env[i]; i++)
  {
    free(env[i]);
  }
  free(env);
}
