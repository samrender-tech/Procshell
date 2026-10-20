#ifndef BUILTINS_H
#define BUILTINS_H

/**
 * @brief Changes the directory
 *
 * @param args the arguments to the directory
 * @param initialDirectory current working directory
 * @return int status: 0 if succeded
 */
int commandCd(char **args, char *initialDirectory);

/**
 * @brief adds the given value to enviornment variable
 * @warning Free required
 *
 * @param args arguments given
 * @param env enviornment variables
 * @return int status: -1 if error else 0
 */
int commandExport(char **args, char ***envReference);

/**
 * @brief removes given key from enviornment variable
 * @warning Free required
 *
 * @param args arguments given
 * @param env reference of new enviornment variable
 *
 * @return int status: -1 if error else 0
 */
int commandUnset(char **args, char ***env);

/**
 * @brief Source command like in bash
 *
 * @param args arguments
 * @param env reference for env variables
 * @param initialDirectory
 * @return int status 0 if success else -1
 */
int commandSource(char **args, char ***env, char *initialDirectory);

// ! My implemented commands
/**
 * @brief handles ai commands
 *
 * @param args arguments given
 *
 * @return int status: -1 if error else 0
 */
int commandAI(char **args);

/**
 * @brief prints current working directory
 *
 * @return int status: if success returns 0
 */
int commandPwd();

/**
 * @brief echos the given arguments
 *
 * @param args the arguments from terminal
 * @param env enviornment variables
 * @return int status: if success returns 0
 */
int commandEcho(char **args, char **env);

/**
 * @brief prints the enviornment variables
 *
 * @param envp enviornment variables
 * @return int success: if success returns 0
 */
int commandEnv(char **envp);

/**
 * @brief Finds the directory of a given command
 *
 * @param args the arguments from terminal
 * @param env enviornment variables needed to find path
 * @return int status: if success returns 0
 */
int commandWhich(char **args, char **env);

#endif