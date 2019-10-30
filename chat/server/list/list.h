#ifndef __LIST
#define __LIST

#include "../server_types.h"
#include <pthread.h>

struct node_t {
  client_t *data;
  struct node_t *next;
  struct node_t *prev;
};

struct doubly_linked_list_t {
  struct node_t *head;
  struct node_t *tail;
  pthread_mutex_t mutex;
};

struct doubly_linked_list_t* lcreate();

int lpushb(struct doubly_linked_list_t* dll, client_t *value);
int lpushf(struct doubly_linked_list_t* dll, client_t *value);
client_t *lpopf(struct doubly_linked_list_t* dll, int *err_code);
client_t *lpopb(struct doubly_linked_list_t* dll, int *err_code);

int lsize(const struct doubly_linked_list_t* dll);
int lempty(const struct doubly_linked_list_t* dll);

client_t *lremove(struct doubly_linked_list_t* dll, unsigned int index);

void lclear(struct doubly_linked_list_t* dll);

unsigned int lgetindex(struct doubly_linked_list_t *dll, client_t *value);

#endif