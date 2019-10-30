#ifndef __TYPES
#define __TYPES

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <stdbool.h>

typedef struct sockaddr SA;
typedef struct sockaddr_in SA_IN;

#define PORT 8000
#define BUFFER_LEN 4096
#define MAX_MESSAGE 8192
#define MAX_CONNECTIONS 100
#define HTTP_200 "HTTP/1.0 200 OK\r\n\r\n"
#define HTTP_404 "HTTP/1.0 404 Not Found\r\n\r\n"

#define KB 1024
#define CLIENT_BUFFER_LEN (KB * 8)
#define CLIENTS_PER_THREAD 100
#define FDS_PER_THREAD (CLIENTS_PER_THREAD + 1)
#define PIPE_READ 0
#define PIPE_WRITE 1
#define PIPE_ADD 0
#define PIPE_REMOVE 1
#define PIPE_DATATYPE 0
#define PIPE_INDEX 1
#define PIPE_VAL 2
#define VACANT_FD -1

typedef struct {
  int socket;
  SA address;
  socklen_t address_len;
  pthread_t thread;
  pthread_mutex_t mutex;
  char read_buf[CLIENT_BUFFER_LEN];
  char write_buf[CLIENT_BUFFER_LEN];
  char name[20];
} client_t;

typedef struct {
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_mutex_t pipe_mutex;
  struct pollfd fds[FDS_PER_THREAD];
  int nfds;
  int saved_fds;
  int pipeptr[2];
} worker_t;

typedef struct doubly_linked_list_t list_t;

#endif