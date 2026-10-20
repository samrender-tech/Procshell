#ifndef BASIC_HELPERS_H
#define BASIC_HELPERS_H

#include "parser.h"

/**
 * @brief strcmp
 *
 * @param a string 1
 * @param b string 2
 * @return int 0 if equal, -1 if a < b, 1 if a > b
 */
int myStrcmp(const char *a, const char *b);

/**
 * @brief finds the enviornment variable with a given name in envs
 * @warning Free required
 *
 * @param name the name of key
 * @param env enviornemnt variables
 * @return char* value: value of given key
 */
char *myGetenv(const char *name, char **env);

/**
 * @brief strlen
 *
 * @param str given string
 * @return int len: length of string
 */
int myStrlen(const char *str);

/**
 * @brief case insensitive comparision of 2 strings till n characters
 *
 * @param a string 1
 * @param b string 2
 * @param n length till comparision required
 * @return int 0 if equal, -1 if a < b, 1 if a > b
 */
int myStrnicmp(const char *a, const char *b, int n);

/**
 * @brief duplicates the given string
 * @warning Free required
 *
 * @param str Given string
 * @return char* string: pointer to new string
 */
char *myStrdup(const char *str);

/**
 * @brief copies the content of str2 into str1 with null termination
 * @warning Give enough length for str1 as str2 and \0
 *
 * @param str1 string 1
 * @param str2 string 2
 */
void myStrcpy(char *str1, const char *str2);

/**
 * @brief gives the pointer to the delimiter in the string
 *
 * @param input string
 * @param delimiter character
 * @return char* pointer: pointer to the delimiter
 */
char *myStrchr(const char *input, const char delimiter);

/**
 * @brief Check if the given command is a shell builtin.
 *
 * The built-in commands include:
 *   1. cd
 *   2. export
 *   3. unset
 *   4. quit
 *   5. exit
 *
 * @param command The command string to check.
 * @return true if the command is a builtin, false otherwise.
 */
bool isBuiltin(char *command);

/**
 * @brief Check if the given command is my implemented version
 *
 * My implmemented commands are:
 *    1. echo
 *    2. pwd
 *    3. env
 *    4. which
 *
 * @param command
 * @return true
 * @return false
 */
bool isMyImplementedBuiltin(char *command);

/**
 * @brief Checks if the given token is '&'
 *
 * @param input
 * @return true
 * @return false
 */
bool isBackground(Token *input);

/**
 * @brief checks if given token is a semicolon
 *
 * @param input
 * @return true
 * @return false
 */
bool isSemicolon(Token *input);

/**
 * @brief checks if given token is a && or ||
 *
 * @param input
 * @return true
 * @return false
 */
bool isLogicalOp(Token *input);

/**
 * @brief checks if given token is a pipe |
 *
 * @param input
 * @return true
 * @return false
 */
bool isPipe(Token *input);

/**
 * @brief checks if given token is a less than operator <
 *
 * @param input
 * @return true
 * @return false
 */
bool isLt(Token *input);

/**
 * @brief checks if given token is a greater than operator >
 *
 * @param input
 * @return true
 * @return false
 */
bool isGt(Token *input);

/**
 * @brief Tells if the operator is >> or not
 * 
 * @param input Token * reference
 * @return true 
 * @return false 
 */
bool isAppend(Token *input);

/**
 * @brief is the given token a delimiter: like ; or && or || or < or >
 *
 * @param token given token
 * @return true
 * @return false
 */
bool isDelimiter(Token *token);

#endif