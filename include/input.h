#ifndef INPUT_H
#define INPUT_H
#include "types/forgettingDLL.h"

/**
 * @brief Enables the raw mode for the terminal
 *
 */
void enableRawMode();

/**
 * @brief Disables raw mode on terminal
 *
 */
void disableRawMode();

/**
 * @brief Get the Input String from the terminal
 *
 * @param ForgettingDoublyLinkedList reference history
 * @param input reference to where to store the output
 * 
 * @note if any text written on terminal and window size change it would run whatever is written
 * 
 * @warning Free required
 * @return int status -1 if failed 1 if window size changed and no input written 0 if no error
 */
int getInputString(ForgettingDoublyLinkedList *history, char **input);

#endif