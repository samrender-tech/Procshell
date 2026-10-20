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

/* trace on | off - show what the shell does with the OS for every command */
int commandTrace(char **args);

/* true while trace mode is on */
extern int traceEnabled;

/* Prints one trace line, e.g. traceLog("fork", "child %d", pid). Does nothing when trace is off. */
void traceLog(const char *tag, const char *format, ...);

#endif
