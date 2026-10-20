#ifndef EXECUTORS_H
#define EXECUTORS_H
#include "parser.h"

/**
 * @brief executes the given Command
 *
 * @param command the Command to execute
 * @param env reference to enviornment variables
 * @param initialDirectory current working directory
 * @return int status: returns 0 if success
 */
int executeCommand(Command *command, char ***env, char *initialDirectory);

/**
 * @brief Executes all the command in the pipeline - helper for executeCommand
 *
 * @param command containing pipelines
 * @param env reference to enviornment variables
 * @param initialDirectory current directory
 * @param laststatus reference to last status to update it
 */
int executeAllPipelines(Command *command, char ***env, char *initialDirectory, int *laststatus);

/**
 * @brief executes the pipeline
 *
 * @param pipeline the Pipeline to execute
 * @param env reference to enviornment variables
 * @param initialDirectory current working directory
 * @param isBackground tells if the process is going to be background process or foreground process
 * @return int status: returns 0 if success
 */
int executePipeline(Pipeline *pipeline, char ***env, char *initialDirectory, bool isBackground);

/**
 * @brief Executes the Pipeline Component
 *
 * @param pc Pipeline component
 * @param env reference to enviornment variables
 * @param fds all the file descriptors from piping of pipeline
 * @param pipeCount length of file descriptor array
 * @param i current file descriptor that the pipeline component needs to attach to
 * @param pids reference of array where the child process pid will be stored
 * @param initialDirectory current working directory
 * @param processGroup sets the process group to the function, initiate it with INT_MIN to use
 * @param isBackground tells if the process is going to be background process or foreground process
 * @return int status: returns 0 if success
 */
int executePipelineComponent(
    PipelineComponent *pc, char ***env, int fds[][2],
    int pipeCount, int i, int pids[], char *initialDirectory, int *processGroup, bool isBackground);

#endif