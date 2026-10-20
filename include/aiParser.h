#include "types/ai.h"

/**
 * @brief Parses the aiOutput.txt file in the specified format and gets the result in either AICommands or AIQuestions depending on the type of aiOutput.txt
 *
 * @warning free required of AICommand or AIQuestions
 *
 * @param commands AICommands ** reference - initialized to NULL, no need to allocate memory
 * @param questions AIQuestions ** reference - initialized to NULL, no need to allocate memory
 * @param path const char * - path of file to be opened
 * @return int success - 0 if success else -1
 */
int parseAI(AICommands **commands, AIQuestions **questions, const char *path);