#ifndef FORGETTING_DLL_H
#define FORGETTING_DLL_H
#include "types/forgettingDLL.h"

/**
 * @brief Creates and initializes a Forgetting Doubly Linkedlist.
 *
 * @return A pointer to the newly created FDLL, or NULL on failure.
 */
ForgettingDoublyLinkedList *createFDLL(int capacity);

/**
 * @brief Creates a listNode
 *
 * @param value Duplicates the given string and creates node
 * @param prev prev node
 * @param next next node
 * @return ListNode node: NULL if error
 */
ListNode *createNode(char *value, ListNode *prev, ListNode *next);

/**
 * @brief Inserts the value into the FDLL
 * @warning duplicates the value string
 *
 * @param ll ForgettingDoublyLinkedList
 * @param value string value
 * @return int -1 if error else 0
 */
int insertInFDLL(ForgettingDoublyLinkedList *ll, char *value);

/**
 * @brief Get the Prev node of curr
 *
 * @warning if previous node not found returns the same node
 * @param curr The current listnode
 * @return ListNode* previous node
 */
ListNode *getPrevNode(ListNode *curr);

/**
 * @brief Get the Next node of curr
 *
 * @param curr The current listnode
 * @return ListNode* next node
 */
ListNode *getNextNode(ListNode *curr);

/**
 * @brief Frees all memory associated with the ForgettingDoublyLinkedList
 *
 * @param ll A pointer to the ForgettingDoublyLinkedList to destroy.
 */
void freeFDLL(ForgettingDoublyLinkedList *ll);

/**
 * @brief Frees the given list node
 *
 * @param node node to free
 */
void freeListNode(ListNode *node);

#endif