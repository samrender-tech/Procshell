#ifndef FORGETTING_DLL_STRUCTURES_H
#define FORGETTING_DLL_STRUCTURES_H

/**
 * @brief ListNode for Forgetting doubly linked list
 *
 * char * command
 *
 * ListNode *prev and next pointers
 */
typedef struct ListNode
{
  char *command;
  struct ListNode *prev, *next;
} ListNode;

/**
 * @brief Forgetting doubly linked list
 *
 * When initialized with a certain capacity maintains it forgetting the past values
 */
typedef struct ForgettingDoublyLinkedList
{
  int size;
  int capacity;
  ListNode *head, *tail;
} ForgettingDoublyLinkedList;

#endif