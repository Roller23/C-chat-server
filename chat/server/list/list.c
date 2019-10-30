#include <stdlib.h>
#include <stdio.h>
#include "list.h"

struct doubly_linked_list_t *lcreate() {
  struct doubly_linked_list_t *list = (struct doubly_linked_list_t *)malloc(sizeof(struct doubly_linked_list_t));
  if (!list) return NULL;
  list->head = NULL;
  list->tail = NULL;
  pthread_mutex_init(&list->mutex, NULL);
  return list;
}

struct node_t *get(const struct doubly_linked_list_t *list, unsigned int index) {
  struct node_t *current = list->head;
  unsigned int i = 0;
  while (current) {
    if (i == index) break;
    current = current->next;
    i++;
  }
  return current;
}

int lpushb(struct doubly_linked_list_t *dll, client_t *value) {
  if (!dll) return 1;
  struct node_t *node = (struct node_t *)malloc(sizeof(struct node_t));
  if (!node) return 2;
  node->data = value;
  node->next = NULL;
  if (!dll->head) {
    dll->head = dll->tail = node;
    dll->tail->next = dll->tail->prev = dll->head->next = dll->head->prev = NULL;
  } else {
    node->prev = dll->tail;
    dll->tail->next = node;
    dll->tail = dll->tail->next;
  }
  return 0;
}
int lpushf(struct doubly_linked_list_t* dll, client_t *value) {
  if (!dll) return 1;
  struct node_t *node = (struct node_t *)malloc(sizeof(struct node_t));
  if (!node) return 2;
  node->data = value;
  node->next = NULL;
  if (!dll->head) {
    dll->head = dll->tail = node;
    dll->tail->next = dll->tail->prev = dll->head->next = dll->head->prev = NULL;
  } else {
    node->next = dll->head;
    dll->head->prev = node;
    dll->head = node;
    dll->head->prev = NULL;
  }
  return 0;
}

client_t *lpopb(struct doubly_linked_list_t *dll, int *err_code) {
  if (!dll || lempty(dll)) {
    if (err_code) *err_code = 1;
    return NULL;
  }
  client_t *value = dll->tail->data;
  if (dll->head == dll->tail) {
    lclear(dll);
    if (err_code) *err_code = 0;
    return value;
  }
  struct node_t *current = dll->head;
  while (current->next != dll->tail) {
    current = current->next;
  }
  free(dll->tail);
  dll->tail = current;
  dll->tail->next = NULL;
  if (err_code) *err_code = 0;
  return value;
}

client_t *lpopf(struct doubly_linked_list_t *dll, int *err_code) {
  if (!dll || lempty(dll)) {
    if (err_code) *err_code = 1;
    return NULL;
  }
  client_t *value = dll->head->data;
  if (dll->head == dll->tail) {
    lclear(dll);
    if (err_code) *err_code = 0;
    return value;
  }
  struct node_t *next = dll->head->next;
  free(dll->head);
  dll->head = next;
  dll->head->prev = NULL;
  if (err_code) *err_code = 0;
  return value;
}

int lsize(const struct doubly_linked_list_t* dll) {
  if (!dll) return -1;
  if (lempty(dll)) return 0;
  int len = 0;
  struct node_t *current = dll->head;
  while (current) {
    current = current->next;
    len++;
  }
  return len;
}

unsigned int lgetindex(struct doubly_linked_list_t *dll, client_t *value) {
  unsigned int index = 0;
  struct node_t *current = dll->head;
  while (current) {
    if (current->data == value) {
      break;
    }
    current = current->next;
    index++;
  }
  return index;
}

int lempty(const struct doubly_linked_list_t* dll) {
  if (!dll) return -1;
  return !dll->head;
}

client_t *lremove(struct doubly_linked_list_t* dll, unsigned int index) {
  if (!dll || lempty(dll) || (int)index > lsize(dll) || (int)index < 0) {
    return NULL;
  }
  if (!index) {
    return lpopf(dll, NULL);
  } else if ((int)index + 1 == lsize(dll)) {
    return lpopb(dll, NULL);
  } else {
    struct node_t *current = get(dll, index);
    client_t *data = current->data;
    struct node_t *prev = current->prev;
    struct node_t *next = current->next;
    free(current);
    prev->next = next;
    next->prev = prev;
    return data;
  }
  return NULL;
}

void lclear(struct doubly_linked_list_t* dll) {
  if (!dll || lempty(dll)) return;
  struct node_t *current = dll->head;
  while (current) {
    struct node_t *next = current->next;
    free(current);
    current = next;
  }
  dll->tail = NULL;
  dll->head = NULL;
  pthread_mutex_destroy(&dll->mutex);
}