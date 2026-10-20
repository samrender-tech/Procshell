#ifndef HELPERS_H
#define HELPERS_H
#include "./parser.h"
#include "types/forgettingDLL.h"

/**
 * @brief Signal interrupt handler
 *
 * @param sig signal code
 */
void handleSignal(int sig);

/**
 * @brief prints the initial info of the shell
 * eg. curr working directory and user name
 */
void printShellStart(char **env, char *userName);

/**
 * @brief Gets the caret position from currPosition to seekPosition in a buffer based on size of terminal
 *
 * @param currPosition
 * @param seekPosition
 * @param termCols number of columns in terminal
 */
void seek(int currPosition, int seekPosition, int termCols);

/**
 * @brief clears text till len length
 *
 * @param len how many characters to clear
 * @param termCols number of columns in terminal
 */
void clearText(int len, int termCols);

/**
 * @brief Prints the given prompt to the shell
 *
 * @param prompt string to print
 */
void printPrompt(char *prompt);

/**
 * @brief writes the input buffer in non canonical mode in the buffer with yellow color
 *
 * @param buffer buffer array
 * @param length length of buffer array to be printed
 * @param cursorPosition current position of cursor
 * @param termCols number of columns in terminal
 */
void writeBufferOnTerminal(char *buffer, int length, int termCols);

/**
 * @brief Get the Full Path Of given command
 * @warning Free required
 *
 * @param command command whose full path needs to be found
 * @param env enviornment variables
 * @return char* path
 */
char *getFullPathOfWhich(char *command, char **env);

/**
 * @brief clones the given enviornment variables
 * @warning Free required
 *
 * @param env original enviornment variables
 * @param newEnv no need to allocate memory: where to place cloned env
 * @return int status: -1 if error else 0
 */
int cloneEnv(char **env, char ***newEnvReference);

/**
 * @brief Writes N history elements from linked list into file
 *
 * @param file name of file
 * @param n number of history elements to write
 * @param history doubly linked list
 * @return int -1 if error else 0
 */
int writeNHistoryInfoToFile(const char *file, int n, ForgettingDoublyLinkedList *history);

/**
 * @brief Writes the platform into specified
 *
 * @param file file name
 * @return int -1 if error else 0
 */
int writePlatformInfoToFile(const char *file);

/**
 * @brief Writes the specified error in the file
 *
 * @param file name of file
 * @param err error to write
 * @return int -1 if error else 0
 */
int writeErrorInfoToFile(const char *file, const char *err);

/**
 * @brief Handles the built in commands
 *
 * @param args Arguments given via terminal
 * @param env enviornment variables
 * @param initialDirectory current working directory
 * @return int status: returns 0 if success
 */
int handleBuiltin(char **args, char ***env, char *initialDirectory);

/**
 * @brief Kills the processes in the array
 *
 * @param start start of the array index
 * @param end till where
 * @param pids process ids
 */
void killPids(int start, int end, int pids[]);

/**
 * @brief Handles my implemented commands
 *
 * @param args Arguments given via terminal
 * @param env enviornment variables
 * @param initialDirectory current working directory
 * @return int status: returns 0 if success
 */
int handleMyImplementedBuiltin(char **args, char ***env, char *initialDirectory);

/**
 * @brief closes all the pipes in the filedescriptor array
 *
 * @param fds file descriptors
 * @param n length
 */
void closePipes(int fds[][2], int n);

/**
 * @brief Given a Pipeline component finds if there is any redirectioin operator in the command
 *
 * If there's a redirection operator then what is the input file or output file
 *
 * Which index does the actual command end at
 *
 * @param pc Pipeline component
 * @param infile reference to input file
 * @param outfile reference to output file
 * @param commandEnd reference to integer of commandend
 *
 * @return isAppendWrite: if outfile exists and the mode is append then true else false
 */
bool findInOutFileAndCommandEnd(
    PipelineComponent *pc, char **infile, char **outfile, int *commandEnd);

/**
 * @brief Free previous enviornment variable
 *
 * @param env array of enviornment variables
 */
void freeEnv(char **env);

/**
 * @brief Free the given token
 *
 * @param token
 */
void freeToken(Token *token);

/**
 * @brief Free all the contents of token vector
 *
 * @param vec
 */
void freeVecToken(VectorToken *vec);

/**
 * @brief Frees all the content of pipeline_component
 *
 * @param pc
 */
void freePipelineComponent(PipelineComponent *pc);

/**
 * @brief Free all the contents of pipeline_component vector
 *
 * @param vec
 */
void freeVecPipelineComponent(VectorPipelineComponent *vec);

/**
 * @brief Free all the contents of pipeline
 *
 * @param p
 */
void freePipeline(Pipeline *p);

/**
 * @brief Free all the contents of pipeline vector
 *
 * @param vec
 */
void freeVecPipeline(VectorPipeline *vec);

/**
 * @brief Free all the contents of command
 *
 * @param c
 */
void freeCommand(Command *c);

/**
 * @brief Free all the contents of command vector
 *
 * @param vec
 */
void freeVecCommand(VectorCommand *vec);

/**
 * @brief Free all the contents of Commands
 *
 * @param c
 */
void freeCommands(Commands *cs);

/**
 * @brief Initializes the AIQuestions structure with questions array with size MAX_AI_QUESTIONS and rest params with null
 *
 * @return AIQuestions*
 */
AIQuestions *createAIQuestions();

/**
 * @brief Initializes the AICommands structure with commands array with size MAX_AI_COMMANDS and rest params with null
 *
 */
AICommands *createAICommand();

/**
 * @brief Frees everything inside AICommands structure
 *
 * @param commands
 */
void freeAICommands(AICommands *commands);

/**
 * @brief Frees everything inside AIQuestions structure
 *
 * @param questions
 */
void freeAIQuestions(AIQuestions *questions);

#endif