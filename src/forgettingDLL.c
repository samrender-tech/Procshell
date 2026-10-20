#include "forgettingDLL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ForgettingDoublyLinkedList *createFDLL(int capacity)
{
  if (capacity <= 0)
  {
    return NULL;
  }

  ForgettingDoublyLinkedList *fdll =
      (ForgettingDoublyLinkedList *)malloc(sizeof(ForgettingDoublyLinkedList));

  if (!fdll)
  {
    return NULL;
  }

  fdll->capacity = capacity;
  fdll->head = NULL;
  fdll->tail = NULL;
  fdll->size = 0;

  return fdll;
}

ListNode *createNode(char *value, ListNode *prev, ListNode *next)
{
  if (value == NULL)
  {
    return NULL;
  }
  ListNode *node = (ListNode *)malloc(sizeof(ListNode));
  if (!node)
  {
    return NULL;
  }

  node->command = strdup(value);
  node->prev = prev;
  node->next = next;

  return node;
}

int insertInFDLL(ForgettingDoublyLinkedList *ll, char *value)
{
  if (!value || !ll)
  {
    return -1;
  }

  if (ll->size == ll->capacity)
  {
    ListNode *temp = ll->head;
    ll->head = ll->head->next;
    freeListNode(temp);

    if (ll->head)
      ll->head->prev = NULL;
    ll->size--;
  }
  ListNode *newNode = createNode(value, ll->tail, NULL);
  if (!newNode)
  {
    return -1;
  }

  if (ll->tail)
  {
    ll->tail->next = newNode;
  }
  if (!ll->head)
  {
    ll->head = newNode;
  }
  ll->tail = newNode;

  ll->size++;
  return 0;
}

ListNode *getPrevNode(ListNode *curr)
{
  if (!curr)
  {
    return NULL;
  }

  if (!curr->prev)
  {
    return curr;
  }

  return curr->prev;
}

ListNode *getNextNode(ListNode *curr)
{
  if (!curr)
  {
    return NULL;
  }

  return curr->next;
}

void freeListNode(ListNode *node)
{
  if (!node)
    return;

  free(node->command);
  free(node);
}

void freeFDLL(ForgettingDoublyLinkedList *ll)
{
  ListNode *temp = ll->head;

  while (temp)
  {
    ListNode *next = temp->next;
    freeListNode(temp);
    temp = next;
  }

  free(ll);
}