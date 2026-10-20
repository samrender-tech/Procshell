#ifndef AI_H
#define AI_H

/**
 * @brief Structure to hold commands from AI
 *
 * char **commands - hold array of commands (array of string)
 *
 * int commandsCount - number of commands
 * 
 * char *warning - if any warning(string)
 *
 * char *explanation - what is the command going to do (string)
 */
typedef struct AICommands
{
   char **commands;
   int commandsCount;
   char *warning;
   char *explanation;
} AICommands;

/**
 * @brief Structure to hold the questions from AI
 * 
 * char ** questions - array to hold questions(array of string)
 * 
 * int questionsCount - number of questions
 *
 * char *explanation - why the question exist(string)
 */
typedef struct AIQuestions
{
   char **questions;
   int questionsCount;
   char *explanation;
} AIQuestions;

#endif