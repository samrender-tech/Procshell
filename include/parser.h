#ifndef PARSER_H
#define PARSER_H
#include "types/parser.h"
#include <stdbool.h>

/**
 * @brief Extract the tokens from input
 * @warning Free required
 *
 * @param input input string
 * @param env enviornment variable array
 * @return VectorToken* Vector of tokens
 */
VectorToken *getTokens(char *input, char **env);

/**
 * @brief expands the token and evaluates $ENV
 * @warning Free required
 *
 * @param input input string
 * @param env enviornment variable array
 * @return Token* token
 */
Token *expandToken(Token *token, bool isOperator, bool isDQuotes, bool isSQuotes, char **env);

/**
 * @brief Splits the tokens into parsed output
 * @warning Free required
 *
 * @param tokenVec vector of tokens
 * @return Commands* All the commands nested and ready to be used
 */
Commands *splitCommands(VectorToken *tokenVec);

/**
 * @brief Create a Token
 * @warning Free required
 *
 * @param str string to copy to new token
 * @param isOperator is it an operator
 * @param capacity capacity of string (capacity >= len)
 * @param len length of string
 * @return Token*
 */
Token *createToken(char *str, int len, bool isOperator, int capacity);

/**
 * @brief insert the given character into the token char array
 *
 * @param token Reference to the token
 * @param c character to insert
 * @return int status: return -1 if error else 0
 */
int insertInTokenChar(Token *token, char c);

/**
 * @brief insert the given string into the token char array
 *
 * @param token Reference to the token
 * @param str string to insert
 * @return int status: return -1 if error else 0
 */
int insertInTokenStr(Token *token, char *str);

/**
 * @brief Create a Pipeline Component object
 * @warning Free required
 *
 * @return PipelineComponent*
 */
PipelineComponent *createPipelineComponent();

/**
 * @brief Create a Pipeline object
 * @warning Free required
 *
 * @return Pipeline*
 */
Pipeline *createPipeline();

/**
 * @brief Create a Command object
 * @warning Free required
 *
 * @return Command*
 */
Command *createCommand();

/**
 * @brief Create a Commands object
 * @warning Free required
 *
 * @return Commands*
 */
Commands *createCommands();

#endif