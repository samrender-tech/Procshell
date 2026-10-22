/*
 * Procshell's own commands, built directly on POSIX system calls.
 *
 *   display  -> opendir(), readdir(), lstat()        (like ls)
 *   show     -> open(), read(), write()              (like cat)
 *   makedir  -> mkdir()                              (like mkdir)
 *   makefile -> open(O_CREAT), futimens()            (like touch)
 *   remove   -> unlink(), rmdir()                    (like rm)
 *   copy     -> open(), read(), write(), fstat()     (like cp)
 *   move     -> rename()                             (like mv)
 *   fileinfo -> lstat(), getpwuid(), getgrgid()      (like stat)
 *   (tree, search, diskuse, count, peek, chmode, makelink: see the second half of this file)
 */

#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "procshell.h"
#include "mycommands.h"

int traceEnabled = 0;

/* ---------- small helpers ---------- */

static bool useColor(int fd)
{
  return isatty(fd);
}

/* "README.md" from "docs/README.md" */
static const char *baseName(const char *path)
{
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

/* 1536 -> "1.5K" */
static void humanSize(off_t bytes, char *out, size_t outLen)
{
  const char *units[] = {"B", "K", "M", "G", "T"};
  double size = (double)bytes;
  int unit = 0;
  while (size >= 1024 && unit < 4)
  {
    size /= 1024;
    unit++;
  }
  if (unit == 0)
    snprintf(out, outLen, "%lldB", (long long)bytes);
  else
    snprintf(out, outLen, "%.1f%s", size, units[unit]);
}

/* st_mode -> "drwxr-xr-x" */
static void permissionString(mode_t mode, char out[11])
{
  out[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : '-';
  const char *rwx = "rwxrwxrwx";
  for (int i = 0; i < 9; i++)
    out[i + 1] = (mode & (1 << (8 - i))) ? rwx[i] : '-';
  out[10] = '\0';
}

static void formatTime(time_t t, const char *format, char *out, size_t outLen)
{
  struct tm *tm = localtime(&t);
  if (!tm || strftime(out, outLen, format, tm) == 0)
    snprintf(out, outLen, "?");
}

/* "<dir>/<name>" - caller frees */
static char *joinPath(const char *dir, const char *name)
{
  size_t len = strlen(dir) + 1 + strlen(name) + 1;
  char *path = malloc(len);
  if (path)
    snprintf(path, len, "%s/%s", dir, name);
  return path;
}

/* write() can write less than asked - keep going until everything is written */
static int writeAll(int fd, const char *buf, ssize_t len)
{
  while (len > 0)
  {
    ssize_t w = write(fd, buf, len);
    if (w == -1)
    {
      if (errno == EINTR)
        continue;
      return -1;
    }
    buf += w;
    len -= w;
  }
  return 0;
}

/* Asks a yes/no question on the terminal. Default is no. */
static bool confirm(const char *question, const char *name)
{
  printf("%s '%s'? [y/N]: ", question, name);
  fflush(stdout);
  char *line = NULL;
  size_t n = 0;
  ssize_t r = getline(&line, &n, stdin);
  bool yes = r > 0 && (line[0] == 'y' || line[0] == 'Y');
  free(line);
  return yes;
}

/* ---------- trace ---------- */

void traceLog(const char *tag, const char *format, ...)
{
  if (!traceEnabled)
    return;

  bool color = useColor(STDERR_FILENO);
  char label[16];
  snprintf(label, sizeof(label), "[%s]", tag);
  fprintf(stderr, "%s%-11s%s", color ? "\033[35m" : "", label, color ? "\033[0m" : "");

  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  fflush(stderr);
}

int commandTrace(char **args)
{
  if (args[1] == NULL)
    traceEnabled = !traceEnabled;
  else if (strcmp(args[1], "on") == 0)
    traceEnabled = 1;
  else if (strcmp(args[1], "off") == 0)
    traceEnabled = 0;
  else
  {
    fprintf(stderr, "usage: trace [on|off]\n");
    return 1;
  }
  printf("trace is %s\n", traceEnabled ? "ON - showing fork, pipe, exec, wait, signals and stats" : "OFF");
  return 0;
}

/* ---------- help ---------- */

int commandHelp(char **args)
{
  (void)args;
  bool c = useColor(STDOUT_FILENO);
  const char *b = c ? "\033[1m" : "", *r = c ? "\033[0m" : "", *h = c ? "\033[1;36m" : "";

  printf("%sProcshell commands%s\n\n", h, r);

  printf("%sFiles%s (written with system calls, not the system's programs)\n", b, r);
  printf("  display [-a] [folder]       list files with size, permissions and date   (like ls)\n");
  printf("  show <file>...              print a file                                (like cat)\n");
  printf("  makedir <folder>...         create folders                              (like mkdir)\n");
  printf("  makefile <file>...          create empty files / update their time      (like touch)\n");
  printf("  remove [-y] <path>...       delete files or empty folders, asks first   (like rm)\n");
  printf("  copy <source> <dest>        copy a file                                 (like cp)\n");
  printf("  move <source> <dest>        move or rename                              (like mv)\n");
  printf("  fileinfo <path>...          inode, size, owner, permissions, times      (like stat)\n");
  printf("  tree [-d depth] [folder]    folder tree                                 (like tree)\n");
  printf("  search <text> [folder]      find files whose name contains text         (like find)\n");
  printf("  diskuse [folder]            total size and the biggest items            (like du)\n");
  printf("  count [file]...             lines, words and bytes                      (like wc)\n");
  printf("  peek [-n N] [file]...       first N lines of a file                     (like head)\n");
  printf("  chmode <octal> <path>...    change permissions, e.g. chmode 755 run.sh  (like chmod)\n");
  printf("  makelink [-s] <target> <name>  hard or symbolic link                    (like ln)\n\n");

  printf("%sNavigation%s\n", b, r);
  printf("  where                       show the current folder                     (like pwd)\n");
  printf("  goto [folder]               change folder, home if empty                (like cd)\n\n");

  printf("%sOS insight%s\n", b, r);
  printf("  trace [on|off]              show fork, pipes, exec, wait, signals and CPU/memory stats\n");
  printf("  timeit <command line>       run a command and report time, CPU and memory\n");
  printf("  sysinfo                     OS, CPUs, memory, load average, shell uptime\n");
  printf("  procinfo [pid]              process ids, group, session, priority, resource limits\n");
  printf("  signal [-g] <pid> <signal>  send stop / cont / term / kill ... (signal -l lists them)\n");
  printf("  history [n]                 numbered list of past commands\n\n");

  printf("%sAI%s\n", b, r);
  printf("  ai <request>                turn plain English into commands, e.g. ai make a folder demo\n\n");

  printf("%sShell builtins%s\n", b, r);
  printf("  cd  export  unset  source  echo  pwd  env  which  exit\n\n");

  printf("%sSyntax%s\n", b, r);
  printf("  a | b    pipe          a && b   run b if a succeeds     a || b   run b if a fails\n");
  printf("  a ; b    one by one    a &      run in background       > >> <   redirection\n");
  return 0;
}

/* ---------- display (ls) ---------- */

typedef struct
{
  char *name;
  struct stat st;
  bool statOk;
} DirEntry;

static int compareEntries(const void *a, const void *b)
{
  return strcasecmp(((const DirEntry *)a)->name, ((const DirEntry *)b)->name);
}

static void printEntry(const char *name, const struct stat *st, bool statOk, bool color)
{
  if (!statOk)
  {
    printf("  ?????????? %8s  %12s  %s\n", "?", "?", name);
    return;
  }

  char perms[11], size[16], date[32];
  permissionString(st->st_mode, perms);
  if (S_ISDIR(st->st_mode))
    snprintf(size, sizeof(size), "-");
  else
    humanSize(st->st_size, size, sizeof(size));
  formatTime(st->st_mtime, "%d %b %H:%M", date, sizeof(date));

  const char *start = "", *end = "", *suffix = "";
  if (S_ISDIR(st->st_mode))
  {
    start = "\033[1;34m";
    suffix = "/";
  }
  else if (S_ISLNK(st->st_mode))
  {
    start = "\033[36m";
    suffix = "@";
  }
  else if (st->st_mode & S_IXUSR)
  {
    start = "\033[1;32m";
    suffix = "*";
  }
  if (color && start[0])
    end = "\033[0m";
  else
    start = "";

  printf("  %s %8s  %12s  %s%s%s%s\n", perms, size, date, start, name, suffix, end);
}

int commandDisplay(char **args)
{
  bool showHidden = false;
  const char *target = ".";
  for (int i = 1; args[i]; i++)
  {
    if (strcmp(args[i], "-a") == 0)
      showHidden = true;
    else
      target = args[i];
  }

  bool color = useColor(STDOUT_FILENO);
  struct stat targetStat;
  if (lstat(target, &targetStat) == -1)
  {
    perror(target);
    return 1;
  }
  if (!S_ISDIR(targetStat.st_mode)) // a single file
  {
    printEntry(target, &targetStat, true, color);
    return 0;
  }

  DIR *dir = opendir(target);
  if (!dir)
  {
    perror(target);
    return 1;
  }

  size_t count = 0, capacity = 32;
  DirEntry *entries = malloc(capacity * sizeof(DirEntry));
  if (!entries)
  {
    closedir(dir);
    return 1;
  }

  struct dirent *de;
  while ((de = readdir(dir)) != NULL)
  {
    if (de->d_name[0] == '.' && !showHidden)
      continue;
    if (count == capacity)
    {
      capacity *= 2;
      DirEntry *grown = realloc(entries, capacity * sizeof(DirEntry));
      if (!grown)
        break;
      entries = grown;
    }
    entries[count].name = strdup(de->d_name);
    char *path = joinPath(target, de->d_name);
    entries[count].statOk = path && lstat(path, &entries[count].st) == 0;
    free(path);
    count++;
  }
  closedir(dir);

  qsort(entries, count, sizeof(DirEntry), compareEntries);

  int folders = 0, files = 0;
  off_t totalBytes = 0;
  for (size_t i = 0; i < count; i++)
  {
    printEntry(entries[i].name, &entries[i].st, entries[i].statOk, color);
    if (entries[i].statOk && S_ISDIR(entries[i].st.st_mode))
      folders++;
    else
    {
      files++;
      if (entries[i].statOk)
        totalBytes += entries[i].st.st_size;
    }
    free(entries[i].name);
  }
  free(entries);

  char total[16];
  humanSize(totalBytes, total, sizeof(total));
  printf("\n  %d folder%s, %d file%s, %s\n", folders, folders == 1 ? "" : "s", files, files == 1 ? "" : "s", total);
  return 0;
}

/* ---------- show (cat) ---------- */

static int copyFdToFd(int in, int out)
{
  char buf[8192];
  for (;;)
  {
    ssize_t r = read(in, buf, sizeof(buf));
    if (r == 0)
      return 0;
    if (r == -1)
    {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (writeAll(out, buf, r) == -1)
      return -1;
  }
}

int commandShow(char **args)
{
  if (args[1] == NULL) // no file: read from stdin, so "display | show" works
    return copyFdToFd(STDIN_FILENO, STDOUT_FILENO) == 0 ? 0 : 1;

  int status = 0;
  for (int i = 1; args[i]; i++)
  {
    int fd = open(args[i], O_RDONLY);
    if (fd == -1)
    {
      perror(args[i]);
      status = 1;
      continue;
    }
    if (copyFdToFd(fd, STDOUT_FILENO) == -1)
    {
      perror(args[i]);
      status = 1;
    }
    close(fd);
  }
  return status;
}

/* ---------- makedir (mkdir) / makefile (touch) ---------- */

int commandMakedir(char **args)
{
  if (args[1] == NULL)
  {
    fprintf(stderr, "usage: makedir <folder>...\n");
    return 1;
  }
  int status = 0;
  for (int i = 1; args[i]; i++)
  {
    if (mkdir(args[i], 0755) == -1)
    {
      perror(args[i]);
      status = 1;
    }
    else
      printf("created folder %s/\n", args[i]);
  }
  return status;
}

int commandMakefile(char **args)
{
  if (args[1] == NULL)
  {
    fprintf(stderr, "usage: makefile <file>...\n");
    return 1;
  }
  int status = 0;
  for (int i = 1; args[i]; i++)
  {
    int fd = open(args[i], O_WRONLY | O_CREAT, 0644);
    if (fd == -1)
    {
      perror(args[i]);
      status = 1;
      continue;
    }
    futimens(fd, NULL); // set access/modify time to now, like touch
    close(fd);
  }
  return status;
}

/* ---------- remove (rm) ---------- */

int commandRemove(char **args)
{
  bool askFirst = true;
  int start = 1;
  if (args[1] && strcmp(args[1], "-y") == 0)
  {
    askFirst = false;
    start = 2;
  }
  if (args[start] == NULL)
  {
    fprintf(stderr, "usage: remove [-y] <file|empty folder>...\n");
    return 1;
  }

  int status = 0;
  for (int i = start; args[i]; i++)
  {
    struct stat st;
    if (lstat(args[i], &st) == -1)
    {
      perror(args[i]);
      status = 1;
      continue;
    }
    bool isDir = S_ISDIR(st.st_mode);
    if (askFirst && !confirm(isDir ? "remove folder" : "remove file", args[i]))
    {
      printf("skipped %s\n", args[i]);
      continue;
    }
    if ((isDir ? rmdir(args[i]) : unlink(args[i])) == -1)
    {
      if (isDir && (errno == ENOTEMPTY || errno == EEXIST))
        fprintf(stderr, "%s: folder is not empty\n", args[i]);
      else
        perror(args[i]);
      status = 1;
    }
    else
      printf("removed %s\n", args[i]);
  }
  return status;
}

/* ---------- copy (cp) / move (mv) ---------- */

/* If dest is a folder, the result goes inside it with the source's name. Caller frees. */
static char *resolveDestination(const char *src, const char *dest)
{
  struct stat st;
  if (stat(dest, &st) == 0 && S_ISDIR(st.st_mode))
    return joinPath(dest, baseName(src));
  return strdup(dest);
}

static int copyFile(const char *src, const char *dest)
{
  int in = open(src, O_RDONLY);
  if (in == -1)
  {
    perror(src);
    return 1;
  }
  struct stat srcStat;
  if (fstat(in, &srcStat) == -1 || S_ISDIR(srcStat.st_mode))
  {
    fprintf(stderr, "%s: only files can be copied\n", src);
    close(in);
    return 1;
  }

  struct stat destStat;
  if (stat(dest, &destStat) == 0 && destStat.st_dev == srcStat.st_dev && destStat.st_ino == srcStat.st_ino)
  {
    fprintf(stderr, "copy: '%s' and '%s' are the same file\n", src, dest);
    close(in);
    return 1;
  }

  int out = open(dest, O_WRONLY | O_CREAT | O_TRUNC, srcStat.st_mode & 0777);
  if (out == -1)
  {
    perror(dest);
    close(in);
    return 1;
  }
  int result = copyFdToFd(in, out);
  close(in);
  close(out);
  if (result == -1)
  {
    perror(dest);
    return 1;
  }
  return 0;
}

int commandCopy(char **args)
{
  if (args[1] == NULL || args[2] == NULL)
  {
    fprintf(stderr, "usage: copy <source> <destination>\n");
    return 1;
  }
  char *dest = resolveDestination(args[1], args[2]);
  if (!dest)
    return 1;
  int status = copyFile(args[1], dest);
  if (status == 0)
    printf("copied %s -> %s\n", args[1], dest);
  free(dest);
  return status;
}

int commandMove(char **args)
{
  if (args[1] == NULL || args[2] == NULL)
  {
    fprintf(stderr, "usage: move <source> <destination>\n");
    return 1;
  }
  char *dest = resolveDestination(args[1], args[2]);
  if (!dest)
    return 1;

  int status = 0;
  if (rename(args[1], dest) == -1)
  {
    if (errno == EXDEV) // different disk: copy, then delete the original
    {
      status = copyFile(args[1], dest);
      if (status == 0 && unlink(args[1]) == -1)
      {
        perror(args[1]);
        status = 1;
      }
    }
    else
    {
      perror(args[1]);
      status = 1;
    }
  }
  if (status == 0)
    printf("moved %s -> %s\n", args[1], dest);
  free(dest);
  return status;
}

/* ---------- fileinfo (stat) ---------- */

static const char *fileTypeName(mode_t mode)
{
  if (S_ISREG(mode))
    return "regular file";
  if (S_ISDIR(mode))
    return "directory";
  if (S_ISLNK(mode))
    return "symbolic link";
  if (S_ISFIFO(mode))
    return "pipe (FIFO)";
  if (S_ISSOCK(mode))
    return "socket";
  if (S_ISCHR(mode))
    return "character device";
  if (S_ISBLK(mode))
    return "block device";
  return "unknown";
}

int commandFileinfo(char **args)
{
  if (args[1] == NULL)
  {
    fprintf(stderr, "usage: fileinfo <path>...\n");
    return 1;
  }

  int status = 0;
  for (int i = 1; args[i]; i++)
  {
    struct stat st;
    if (lstat(args[i], &st) == -1)
    {
      perror(args[i]);
      status = 1;
      continue;
    }

    char perms[11], size[16], accessed[64], modified[64], changed[64];
    permissionString(st.st_mode, perms);
    humanSize(st.st_size, size, sizeof(size));
    formatTime(st.st_atime, "%d %b %Y %H:%M:%S", accessed, sizeof(accessed));
    formatTime(st.st_mtime, "%d %b %Y %H:%M:%S", modified, sizeof(modified));
    formatTime(st.st_ctime, "%d %b %Y %H:%M:%S", changed, sizeof(changed));
    struct passwd *pw = getpwuid(st.st_uid);
    struct group *gr = getgrgid(st.st_gid);

    printf("  File:        %s\n", args[i]);
    printf("  Type:        %s\n", fileTypeName(st.st_mode));
    printf("  Size:        %lld bytes (%s)\n", (long long)st.st_size, size);
    printf("  Permissions: %s (%04o)\n", perms, (unsigned)(st.st_mode & 07777));
    printf("  Owner:       %s (uid %d)\n", pw ? pw->pw_name : "?", (int)st.st_uid);
    printf("  Group:       %s (gid %d)\n", gr ? gr->gr_name : "?", (int)st.st_gid);
    printf("  Inode:       %llu\n", (unsigned long long)st.st_ino);
    printf("  Hard links:  %llu\n", (unsigned long long)st.st_nlink);
    printf("  Accessed:    %s\n", accessed);
    printf("  Modified:    %s\n", modified);
    printf("  Changed:     %s\n", changed);
    if (args[i + 1])
      printf("\n");
  }
  return status;
}

/* ======================================================================
 * More file-system commands
 *
 *   tree     -> opendir(), readdir(), lstat()  recursively      folder tree
 *   search   -> opendir(), readdir()  recursively               find by name
 *   diskuse  -> lstat() st_size  recursively                    folder sizes
 *   count    -> read()                                          lines / words / bytes
 *   peek     -> read(), write()                                 first N lines
 *   chmode   -> stat(), chmod()                                 change permissions
 *   makelink -> link(), symlink(), lstat()                      hard and symbolic links
 * ====================================================================== */

#define MAX_TREE_DEPTH 32

static bool isHiddenName(const char *name)
{
  return name[0] == '.';
}

/* Sorted list of the visible names in a folder; caller frees with freeNames. */
static char **listFolder(const char *path, size_t *countOut, bool includeHidden)
{
  *countOut = 0;
  DIR *dir = opendir(path);
  if (!dir)
    return NULL;

  size_t count = 0, capacity = 16;
  char **names = malloc(capacity * sizeof(char *));
  struct dirent *de;
  while (names && (de = readdir(dir)) != NULL)
  {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;
    if (!includeHidden && isHiddenName(de->d_name))
      continue;
    if (count == capacity)
    {
      capacity *= 2;
      char **grown = realloc(names, capacity * sizeof(char *));
      if (!grown)
        break;
      names = grown;
    }
    names[count++] = strdup(de->d_name);
  }
  closedir(dir);

  for (size_t i = 1; i < count; i++) // insertion sort, folders are small
  {
    char *key = names[i];
    size_t j = i;
    while (j > 0 && strcasecmp(names[j - 1], key) > 0)
    {
      names[j] = names[j - 1];
      j--;
    }
    names[j] = key;
  }
  *countOut = count;
  return names;
}

static void freeNames(char **names, size_t count)
{
  if (!names)
    return;
  for (size_t i = 0; i < count; i++)
    free(names[i]);
  free(names);
}

/* ---------- tree ---------- */

typedef struct
{
  int folders;
  int files;
  int maxDepth;
  bool color;
} TreeState;

static void treeWalk(const char *path, const char *prefix, int depth, TreeState *state)
{
  size_t count;
  char **names = listFolder(path, &count, false);
  if (!names)
    return;

  for (size_t i = 0; i < count; i++)
  {
    bool last = (i == count - 1);
    char *child = joinPath(path, names[i]);
    struct stat st;
    bool ok = child && lstat(child, &st) == 0;
    bool isDir = ok && S_ISDIR(st.st_mode);
    bool isLink = ok && S_ISLNK(st.st_mode);

    const char *start = "", *end = "";
    if (state->color && isDir)
    {
      start = "\033[1;34m";
      end = "\033[0m";
    }
    else if (state->color && isLink)
    {
      start = "\033[36m";
      end = "\033[0m";
    }
    printf("%s%s%s%s%s%s\n", prefix, last ? "└── " : "├── ", start, names[i], isDir ? "/" : "", end);

    if (isDir)
    {
      state->folders++;
      if (depth + 1 < state->maxDepth)
      {
        size_t prefixLen = strlen(prefix) + 8;
        char *nextPrefix = malloc(prefixLen);
        if (nextPrefix)
        {
          snprintf(nextPrefix, prefixLen, "%s%s", prefix, last ? "    " : "│   ");
          treeWalk(child, nextPrefix, depth + 1, state);
          free(nextPrefix);
        }
      }
    }
    else
      state->files++;
    free(child);
  }
  freeNames(names, count);
}

int commandTree(char **args)
{
  TreeState state = {0, 0, 3, useColor(STDOUT_FILENO)};
  const char *root = ".";
  for (int i = 1; args[i]; i++)
  {
    if (strcmp(args[i], "-d") == 0 && args[i + 1])
    {
      state.maxDepth = atoi(args[++i]);
      if (state.maxDepth < 1 || state.maxDepth > MAX_TREE_DEPTH)
      {
        fprintf(stderr, "tree: depth must be 1-%d\n", MAX_TREE_DEPTH);
        return 1;
      }
    }
    else
      root = args[i];
  }

  struct stat st;
  if (stat(root, &st) == -1 || !S_ISDIR(st.st_mode))
  {
    fprintf(stderr, "tree: %s: not a folder\n", root);
    return 1;
  }
  printf("%s%s/%s\n", state.color ? "\033[1;34m" : "", root, state.color ? "\033[0m" : "");
  treeWalk(root, "", 0, &state);
  printf("\n%d folder%s, %d file%s (depth %d, hidden items skipped)\n", state.folders, state.folders == 1 ? "" : "s",
         state.files, state.files == 1 ? "" : "s", state.maxDepth);
  return 0;
}

/* ---------- search ---------- */

/* case-insensitive "does haystack contain needle" */
static bool containsIgnoreCase(const char *haystack, const char *needle)
{
  size_t n = strlen(needle);
  for (; *haystack; haystack++)
    if (strncasecmp(haystack, needle, n) == 0)
      return true;
  return n == 0;
}

static void searchWalk(const char *path, const char *text, int depth, int *matches, bool color)
{
  if (depth > MAX_TREE_DEPTH)
    return;
  size_t count;
  char **names = listFolder(path, &count, false);
  if (!names)
    return;

  for (size_t i = 0; i < count; i++)
  {
    char *child = joinPath(path, names[i]);
    if (!child)
      continue;
    struct stat st;
    bool isDir = lstat(child, &st) == 0 && S_ISDIR(st.st_mode);
    if (containsIgnoreCase(names[i], text))
    {
      (*matches)++;
      printf("%s%s%s%s\n", color && isDir ? "\033[1;34m" : "", child, isDir ? "/" : "", color && isDir ? "\033[0m" : "");
    }
    if (isDir)
      searchWalk(child, text, depth + 1, matches, color);
    free(child);
  }
  freeNames(names, count);
}

int commandSearch(char **args)
{
  if (!args[1] || (args[2] && args[3]))
  {
    fprintf(stderr, "usage: search <text> [folder]     finds files whose name contains <text>\n");
    return 1;
  }
  const char *root = args[2] ? args[2] : ".";
  int matches = 0;
  searchWalk(root, args[1], 0, &matches, useColor(STDOUT_FILENO));
  printf("\n%d match%s for '%s' under %s (hidden folders skipped)\n", matches, matches == 1 ? "" : "es", args[1], root);
  return matches > 0 ? 0 : 1;
}

/* ---------- diskuse ---------- */

typedef struct
{
  char *name;
  off_t bytes;
  bool isDir;
} SizedEntry;

static off_t sizeOfPath(const char *path, int depth, long *fileCount)
{
  struct stat st;
  if (lstat(path, &st) == -1)
    return 0;
  if (!S_ISDIR(st.st_mode))
  {
    (*fileCount)++;
    return st.st_size;
  }
  if (depth > MAX_TREE_DEPTH)
    return 0;

  off_t total = 0;
  size_t count;
  char **names = listFolder(path, &count, true);
  for (size_t i = 0; names && i < count; i++)
  {
    char *child = joinPath(path, names[i]);
    if (child)
      total += sizeOfPath(child, depth + 1, fileCount);
    free(child);
  }
  freeNames(names, count);
  return total;
}

static int compareSizeDesc(const void *a, const void *b)
{
  off_t x = ((const SizedEntry *)a)->bytes, y = ((const SizedEntry *)b)->bytes;
  return (x < y) - (x > y);
}

int commandDiskuse(char **args)
{
  const char *root = args[1] ? args[1] : ".";
  struct stat st;
  if (lstat(root, &st) == -1)
  {
    perror(root);
    return 1;
  }

  char total[16];
  long files = 0;
  if (!S_ISDIR(st.st_mode))
  {
    humanSize(st.st_size, total, sizeof(total));
    printf("%s  %s\n", total, root);
    return 0;
  }

  size_t count;
  char **names = listFolder(root, &count, true);
  SizedEntry *entries = calloc(count ? count : 1, sizeof(SizedEntry));
  off_t sum = 0;
  for (size_t i = 0; names && entries && i < count; i++)
  {
    char *child = joinPath(root, names[i]);
    struct stat cst;
    entries[i].name = names[i];
    entries[i].isDir = child && lstat(child, &cst) == 0 && S_ISDIR(cst.st_mode);
    entries[i].bytes = child ? sizeOfPath(child, 1, &files) : 0;
    sum += entries[i].bytes;
    free(child);
  }
  if (entries)
    qsort(entries, count, sizeof(SizedEntry), compareSizeDesc);

  bool color = useColor(STDOUT_FILENO);
  size_t shown = count < 10 ? count : 10;
  for (size_t i = 0; entries && i < shown; i++)
  {
    char size[16];
    humanSize(entries[i].bytes, size, sizeof(size));
    int bar = sum > 0 ? (int)(entries[i].bytes * 20 / sum) : 0;
    char bars[21];
    memset(bars, '#', bar);
    bars[bar] = '\0';
    printf("  %8s  %-20s  %s%s%s%s\n", size, bars, color && entries[i].isDir ? "\033[1;34m" : "", entries[i].name,
           entries[i].isDir ? "/" : "", color && entries[i].isDir ? "\033[0m" : "");
  }
  if (count > shown)
    printf("  ... and %zu more\n", count - shown);

  humanSize(sum, total, sizeof(total));
  printf("\n  total %s in %ld file%s under %s\n", total, files, files == 1 ? "" : "s", root);
  free(entries);
  freeNames(names, count);
  return 0;
}

/* ---------- count ---------- */

typedef struct
{
  long lines, words, bytes;
} Counts;

static int countFd(int fd, Counts *c)
{
  char buf[8192];
  bool inWord = false;
  for (;;)
  {
    ssize_t r = read(fd, buf, sizeof(buf));
    if (r == 0)
      return 0;
    if (r == -1)
    {
      if (errno == EINTR)
        continue;
      return -1;
    }
    c->bytes += r;
    for (ssize_t i = 0; i < r; i++)
    {
      if (buf[i] == '\n')
        c->lines++;
      bool space = isspace((unsigned char)buf[i]);
      if (!space && !inWord)
        c->words++;
      inWord = !space;
    }
  }
}

int commandCount(char **args)
{
  printf("%8s %8s %10s\n", "lines", "words", "bytes");
  if (!args[1]) // no file: count stdin, so "display | count" works
  {
    Counts c = {0, 0, 0};
    if (countFd(STDIN_FILENO, &c) == -1)
      return 1;
    printf("%8ld %8ld %10ld\n", c.lines, c.words, c.bytes);
    return 0;
  }

  Counts total = {0, 0, 0};
  int status = 0, files = 0;
  for (int i = 1; args[i]; i++)
  {
    int fd = open(args[i], O_RDONLY);
    Counts c = {0, 0, 0};
    if (fd == -1 || countFd(fd, &c) == -1)
    {
      perror(args[i]);
      status = 1;
      if (fd != -1)
        close(fd);
      continue;
    }
    close(fd);
    printf("%8ld %8ld %10ld  %s\n", c.lines, c.words, c.bytes, args[i]);
    total.lines += c.lines;
    total.words += c.words;
    total.bytes += c.bytes;
    files++;
  }
  if (files > 1)
    printf("%8ld %8ld %10ld  total\n", total.lines, total.words, total.bytes);
  return status;
}

/* ---------- peek ---------- */

static int peekFd(int fd, long lines)
{
  char buf[4096];
  long seen = 0;
  while (seen < lines)
  {
    ssize_t r = read(fd, buf, sizeof(buf));
    if (r == 0)
      break;
    if (r == -1)
    {
      if (errno == EINTR)
        continue;
      return -1;
    }
    ssize_t upto = r;
    for (ssize_t i = 0; i < r; i++)
    {
      if (buf[i] == '\n' && ++seen == lines)
      {
        upto = i + 1;
        break;
      }
    }
    if (writeAll(STDOUT_FILENO, buf, upto) == -1)
      return -1;
  }
  return 0;
}

int commandPeek(char **args)
{
  long lines = 10;
  int a = 1;
  if (args[1] && strcmp(args[1], "-n") == 0)
  {
    if (!args[2] || (lines = atol(args[2])) <= 0)
    {
      fprintf(stderr, "usage: peek [-n lines] [file]...\n");
      return 1;
    }
    a = 3;
  }

  if (!args[a]) // no file: read stdin, so "history | peek -n 3" works
    return peekFd(STDIN_FILENO, lines) == 0 ? 0 : 1;

  int status = 0;
  for (int i = a; args[i]; i++)
  {
    int fd = open(args[i], O_RDONLY);
    if (fd == -1)
    {
      perror(args[i]);
      status = 1;
      continue;
    }
    if (args[a + 1])
      printf("==> %s <==\n", args[i]);
    fflush(stdout);
    if (peekFd(fd, lines) == -1)
    {
      perror(args[i]);
      status = 1;
    }
    close(fd);
  }
  return status;
}

/* ---------- chmode ---------- */

int commandChmode(char **args)
{
  if (!args[1] || !args[2])
  {
    fprintf(stderr, "usage: chmode <octal mode> <path>...     e.g. chmode 755 run.sh\n");
    return 1;
  }

  char *end;
  long mode = strtol(args[1], &end, 8);
  if (*end != '\0' || mode < 0 || mode > 07777)
  {
    fprintf(stderr, "chmode: '%s' is not an octal mode like 644 or 755\n", args[1]);
    return 1;
  }

  int status = 0;
  for (int i = 2; args[i]; i++)
  {
    struct stat st;
    if (stat(args[i], &st) == -1)
    {
      perror(args[i]);
      status = 1;
      continue;
    }
    char before[11], after[11];
    permissionString(st.st_mode, before);
    if (chmod(args[i], (mode_t)mode) == -1)
    {
      perror(args[i]);
      status = 1;
      continue;
    }
    permissionString((st.st_mode & ~07777) | (mode_t)mode, after);
    printf("%s  %s (%04o) -> %s (%04lo)\n", args[i], before, (unsigned)(st.st_mode & 07777), after, mode);
  }
  return status;
}

/* ---------- makelink ---------- */

int commandMakelink(char **args)
{
  bool symbolic = false;
  int a = 1;
  if (args[1] && strcmp(args[1], "-s") == 0)
  {
    symbolic = true;
    a = 2;
  }
  if (!args[a] || !args[a + 1] || args[a + 2])
  {
    fprintf(stderr, "usage: makelink [-s] <target> <link name>\n");
    fprintf(stderr, "  without -s: hard link  (a second name for the same inode)\n");
    fprintf(stderr, "  with -s:    symbolic link (a small file that points to a path)\n");
    return 1;
  }
  const char *target = args[a], *name = args[a + 1];

  if ((symbolic ? symlink(target, name) : link(target, name)) == -1)
  {
    perror("makelink");
    return 1;
  }

  struct stat st;
  if (symbolic)
  {
    printf("symbolic link %s -> %s\n", name, target);
    if (lstat(name, &st) == 0)
      printf("  the link has its own inode %llu; it stores the path, not the data\n", (unsigned long long)st.st_ino);
  }
  else if (stat(name, &st) == 0)
  {
    printf("hard link %s = %s\n", name, target);
    printf("  both names share inode %llu, link count is now %llu\n", (unsigned long long)st.st_ino,
           (unsigned long long)st.st_nlink);
  }
  return 0;
}
