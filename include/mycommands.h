#ifndef MYCOMMANDS_H
#define MYCOMMANDS_H

/*
 * Procshell's own commands.
 * Each one is written directly on POSIX system calls
 * (opendir, readdir, stat, open, read, write, mkdir, unlink, rename ...)
 * instead of running the system's ls / cat / cp / mv programs.
 *
 * All of them return 0 on success and 1 on failure.
 */

int commandHelp(char **args);     /* help                         - list Procshell commands       */
int commandDisplay(char **args);  /* display [-a] [folder]        - like ls                       */
int commandShow(char **args);     /* show <file>...               - like cat                      */
int commandMakedir(char **args);  /* makedir <folder>...          - like mkdir                    */
int commandMakefile(char **args); /* makefile <file>...           - like touch                    */
int commandRemove(char **args);   /* remove [-y] <file|folder>... - like rm, asks first           */
int commandCopy(char **args);     /* copy <source> <destination>  - like cp                       */
int commandMove(char **args);     /* move <source> <destination>  - like mv                       */
int commandFileinfo(char **args); /* fileinfo <file>...           - like stat                     */

/* More file-system commands (src/mycommands.c) */
int commandTree(char **args);     /* tree [-d depth] [folder]     - folder tree                   */
int commandSearch(char **args);   /* search <text> [folder]       - find files by name (like find)*/
int commandDiskuse(char **args);  /* diskuse [folder]             - total size, biggest items (du)*/
int commandCount(char **args);    /* count [file]...              - lines, words, bytes (like wc) */
int commandPeek(char **args);     /* peek [-n N] [file]           - first N lines (like head)     */
int commandChmode(char **args);   /* chmode <octal> <path>...     - change permissions (chmod)    */
int commandMakelink(char **args); /* makelink [-s] <target> <name> - hard / symbolic link (ln)    */

/* Process and OS commands (src/oscommands.c) */
int commandSysinfo(char **args);  /* sysinfo                      - OS, CPUs, memory, load        */
int commandProcinfo(char **args); /* procinfo [pid]               - ids, priority, limits         */
int commandSignal(char **args);   /* signal [-g] <pid> <signal>   - send a signal (like kill)     */
int commandHistory(char **args);  /* history [n]                  - numbered command history      */
int commandTimeit(char **args, char ***env, char *initialDirectory); /* timeit <command line>     */

/* CPU / memory totals of the processes the executor waited for (filled in by executor.c) */
typedef struct
{
  double realSeconds;
  double userSeconds;
  double systemSeconds;
  double peakMemoryMB;
  int processes;
} RunStats;
extern RunStats runStats;

/* trace on | off - show what the shell does with the OS for every command */
int commandTrace(char **args);

/* true while trace mode is on */
extern int traceEnabled;

/* Prints one trace line, e.g. traceLog("fork", "child %d", pid). Does nothing when trace is off. */
void traceLog(const char *tag, const char *format, ...);

#endif
