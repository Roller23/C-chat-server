#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <netdb.h>

#include "server_types.h"
#include "list/list.h"

pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t workers_mutex = PTHREAD_MUTEX_INITIALIZER;

struct {
  //server variables to be shared between threads
  int socket;
  pthread_t listening_thread;
  list_t *list;
  int cores;
  worker_t *workers;
} server_data;

bool starts_with(char *str1, char *str2) {
  return strncmp(str1, str2, strlen(str2) - 1) == 0;
}

void mem_dump(char *ptr, int length, int size) {
  printf("{");
  for (int i = 0, j = 0; i < length; i++, j += size) {
    printf("%d", *(ptr + j));
    if (i != length - 1) {
      printf(", ");
    }
  }
  printf("}\n");
}

worker_t *get_optimal_worker(void) {
  worker_t *worker = server_data.workers;
  for (int i = 1; i < server_data.cores; i++) {
    if (server_data.workers[i].saved_fds < worker->saved_fds) {
      worker = server_data.workers + i;
    }
  }
  if (worker->saved_fds == CLIENTS_PER_THREAD) {
    //limit reached
    return NULL;
  }
  return worker;
}

void close_client(client_t *client);

int add_client(client_t *client) {
  pthread_mutex_lock(&server_data.list->mutex);
  int err = lpushf(server_data.list, client);
  pthread_mutex_unlock(&server_data.list->mutex);
  return err;
}

void remove_client(client_t *client) {
  pthread_mutex_lock(&server_data.list->mutex);
  unsigned int index = lgetindex(server_data.list, client);
  lremove(server_data.list, index);
  pthread_mutex_unlock(&server_data.list->mutex);
}

int send_msg(client_t *client, const char *msg) {
  int datalen = strlen(msg);
  int data = htonl(datalen);
  int w = write(client->socket, &data, sizeof(data));
  if (w <= 0) {
    return w;
  }
  return write(client->socket, msg, strlen(msg));
}

char *read_msg(client_t *client, int *err) {
  int bufferlen;
  int r = read(client->socket, &bufferlen, sizeof(bufferlen));
  if (r <= 0) {
    if (err) *err = r;
    return NULL;
  }
  bufferlen = ntohl(bufferlen);
  char *newbuf = calloc(bufferlen + 1, sizeof(char));
  int bytes_read = 0;
  while (bufferlen > 0) {
    char *buffer = calloc(bufferlen, sizeof(char));
    int r = read(client->socket, buffer, bufferlen);
    if (r <= 0) {
      if (err) *err = r;
      free(buffer);
      free(newbuf);
      return NULL;
    }
    strncat(newbuf, buffer, bufferlen);
    bufferlen -= r;
    bytes_read += r;
    free(buffer);
  }
  if (err) *err = bytes_read;
  return newbuf;
}

void broadcast_msg(const char *msg, client_t *exclude) {
  pthread_mutex_lock(&server_data.list->mutex);
  struct node_t *current = server_data.list->head;
  while (current) {
    if (exclude != NULL && current->data == exclude) {
      current = current->next;
      continue;
    }
    send_msg(current->data, msg);
    current = current->next;
  }
  pthread_mutex_unlock(&server_data.list->mutex);
}

void logout(client_t *client) {
  int mem = strlen("OUT ") + strlen(client->name) + 1;
  char *buffer = calloc(mem, sizeof(char));
  snprintf(buffer, mem, "OUT %s", client->name);
  printf("%s logged out\n", client->name);
  remove_client(client);
  close_client(client);
  broadcast_msg(buffer, NULL);
  free(buffer);
}

void handle_message(char *message, client_t *client) {
  if (starts_with(message, "MSG")) {
    //broadcast the message to all subscribers
    char *message_offset = message + strlen("MSG ");
    printf("%s sent a message: '%s'\n", client->name, message_offset);
    int mem = strlen(message) + strlen(client->name) + 3;
    char *buffer = calloc(mem, sizeof(char));
    snprintf(buffer, mem, "MSG %s: %s", client->name, message_offset);
    broadcast_msg(buffer, NULL);
    free(buffer);
  }
}

void *listen_client(void *arg) {
  client_t *client = (client_t *)arg;
  while (true) {
    int bytes;
    char *message = read_msg(client, &bytes);
    if (bytes == -1) {
      perror("Message read error");
      close_client(client);
      break;
    }
    if (bytes == 0) {
      printf("%s disconnected from the chat\n", client->name);
      logout(client);
      break;
    }
    if (starts_with(message, "MSG")) {
      //broadcast the message to all subscribers
      char *message_offset = message + strlen("MSG ");
      printf("%s sent a message: '%s'\n", client->name, message_offset);
      int mem = strlen(message) + strlen(client->name) + 3;
      char *buffer = calloc(mem, sizeof(char));
      snprintf(buffer, mem, "MSG %s: %s", client->name, message_offset);
      broadcast_msg(buffer, NULL);
      free(buffer);
    }
    if (starts_with(message, "LOGOUT")) {
      logout(client);
      free(message);
      break;
    }
    free(message);
  }
  return 0;
}

void server_cleanup(void) {
  pthread_cancel(server_data.listening_thread);
  close(server_data.socket);
  pthread_mutex_lock(&server_data.list->mutex);
  lclear(server_data.list);
  pthread_mutex_unlock(&server_data.list->mutex);
  pthread_mutex_lock(&workers_mutex);
  for (int i = 0; i < server_data.cores; i++) {
    pthread_mutex_destroy(&server_data.workers[i].mutex);
    pthread_mutex_destroy(&server_data.workers[i].pipe_mutex);
    pthread_cancel(server_data.workers[i].thread);
  }
  free(server_data.workers);
  pthread_mutex_unlock(&workers_mutex);
  pthread_mutex_destroy(&global_mutex);
  pthread_mutex_destroy(&workers_mutex);
}

void terminate_server(void) {
  server_cleanup();
  exit(0);
}

void close_client(client_t *client) {
  close(client->socket);
  free(client);
}

client_t *getclientbysocket(int fd) {
  pthread_mutex_lock(&server_data.list->mutex);
  struct node_t *current = server_data.list->head;
  client_t *client = NULL;
  while (current) {
    if (current->data->socket == fd) {
      client = current->data;
      break;
    }
    current = current->next;
  }
  pthread_mutex_unlock(&server_data.list->mutex);
  return client;
}

void deletefd(worker_t *worker, int fd) {
  pthread_mutex_lock(&worker->mutex);
  int index = -1;
  for (int i = 0; i < worker->nfds; i++) {
    if (worker->fds[i].fd == fd) {
      index = i;
      break;
    }
  }
  if (index != -1) {
    int data[3];
    data[PIPE_DATATYPE] = PIPE_REMOVE;
    data[PIPE_INDEX] = index;
    data[PIPE_VAL] = fd;
    write(worker->pipeptr[PIPE_WRITE], &data, sizeof(data));
  }
  pthread_mutex_unlock(&worker->mutex);
}

void addfd(worker_t *worker, int fd) {
  pthread_mutex_lock(&worker->mutex);
  int index = -1;
  for (int i = 0; i < worker->nfds; i++) {
    if (worker->fds[i].fd == VACANT_FD) {
      index = i;
      break;
    }
  }
  if (index != -1) {
    pthread_mutex_lock(&worker->pipe_mutex);
    int data[3];
    data[PIPE_DATATYPE] = PIPE_ADD;
    data[PIPE_INDEX] = index;
    data[PIPE_VAL] = fd;
    write(worker->pipeptr[PIPE_WRITE], &data, sizeof(data));
    pthread_mutex_unlock(&worker->pipe_mutex);
  }
  pthread_mutex_unlock(&worker->mutex);
}

void *watch_sockets(void *arg) {
  worker_t *worker = (worker_t *)arg;
  while (true) {
    pthread_mutex_lock(&worker->mutex);
    struct pollfd *fds = worker->fds;
    int nfds = worker->nfds;
    pthread_mutex_unlock(&worker->mutex);
    int res = poll(fds, nfds, -1);
    if (res < 0) {
      perror("Poll error");
      continue;
    }
    pthread_mutex_lock(&worker->mutex);
    for (int i = 0; i < nfds; i++) {
      struct pollfd pfd = fds[i];
      if (pfd.revents & POLLHUP) {
        //socket disconnected
        pfd.revents = 0;
        client_t *client = getclientbysocket(pfd.fd);
        printf("%s disconnected from the chat\n", client->name);
        logout(client);
        deletefd(worker, pfd.fd);
        continue;
      }
      if (pfd.revents & POLLIN) {
        //there is data to read
        pfd.revents = 0;
        if (pfd.fd == worker->pipeptr[PIPE_READ]) {
          //a worker pipe was used to wake up poll()
          pthread_mutex_lock(&worker->pipe_mutex);
          int data[3];
          read(worker->pipeptr[PIPE_READ], &data, sizeof(data));
          if (data[PIPE_DATATYPE] == PIPE_ADD) {
            fds[data[PIPE_INDEX]].fd = data[PIPE_VAL];
            worker->saved_fds++;
          } else if (data[PIPE_DATATYPE] == PIPE_REMOVE) {
            fds[data[PIPE_INDEX]].fd = VACANT_FD;
            worker->saved_fds--;
          }
          pthread_mutex_unlock(&worker->pipe_mutex);
          continue;
        }
        client_t *client = getclientbysocket(pfd.fd);
        int bytes;
        char *message = read_msg(client, &bytes);
        if (bytes == -1) {
          perror("Message read error");
          close_client(client);
          break;
        }
        if (bytes == 0) {
          printf("%s disconnected from the chat\n", client->name);
          logout(client);
          deletefd(worker, pfd.fd);
        }
        handle_message(message, client);
        free(message);
      }
    }
    pthread_mutex_unlock(&worker->mutex);
  }
  return 0;
}

FILE *openfile(const char *name, const char *mode) {
  char *rpath = realpath(name, NULL);
  if (rpath == NULL) {
    return NULL;
  }
  FILE *file = fopen(rpath, mode);
  free(rpath);
  return file;
}

void handle_get(char *request, client_t *client) {
  char response_buf[BUFFER_LEN];
  if (starts_with(request, "/ HTTP")) {
    FILE *html = openfile("webassets/index.html", "r");
    if (!html) {
      perror("read file error");
      close_client(client);
      terminate_server();
    }
    write(client->socket, HTTP_200, strlen(HTTP_200));
    while (fread(response_buf, 1, BUFFER_LEN, html) > 0) {
      write(client->socket, response_buf, strlen(response_buf));
      memset(response_buf, 0, BUFFER_LEN);
    }
    fclose(html);
  } else if (starts_with(request, "/download HTTP")) {
    FILE *file = openfile("downloads/client.c", "r");
    if (!file) {
      perror("read file error");
      close_client(client);
      terminate_server();
    }
    write(client->socket, HTTP_200, strlen(HTTP_200));
    while (fread(response_buf, 1, BUFFER_LEN, file) > 0) {
      write(client->socket, response_buf, strlen(response_buf));
      memset(response_buf, 0, BUFFER_LEN);
    }
    fclose(file);
  } else {
    //unknown path
    char res[] = HTTP_404 "Page not found";
    write(client->socket, res, sizeof(res));
  }
  close_client(client);
}

void *handle_new_connection(void *arg) {
  client_t *client = (client_t *)arg;
  char read_buf[BUFFER_LEN];
  memset(read_buf, 0, BUFFER_LEN);
  int bytes_received = read(client->socket, read_buf, BUFFER_LEN - 1);
  //this server doesn't support long requests, only checking if it's an HTTP request
  if (bytes_received < 0) {
    perror("Request read error");
    close_client(client);
    return 0;
  }
  printf("Received request: %s\n", read_buf);
  if (starts_with(read_buf, "GET /")) {
    handle_get(read_buf + strlen("GET "), client);
    return 0;
  }
  if (starts_with(read_buf, "LOGIN")) {
    worker_t *worker = get_optimal_worker();
    if (worker == NULL) {
      //all workers are busy
      send_msg(client, "BUSY");
      close_client(client);
      return 0;
    }
    char *login = strtok(read_buf, " ");
    login = strtok(NULL, " ");
    strncpy(client->name, login, 20);
    add_client(client);
    addfd(worker, client->socket);
    //signal that there is a new socket to watch

    //old way
    //pthread_create(&client->thread, NULL, listen_client, client);

    send_msg(client, "LOGGED");
    pthread_mutex_lock(&server_data.list->mutex);
    struct node_t *current = server_data.list->tail;
    while (current) {
      int mem = strlen("NEW ") + strlen(current->data->name) + 1;
      char *buf = calloc(mem, sizeof(char));
      snprintf(buf, mem, "NEW %s", current->data->name);
      send_msg(client, buf);
      free(buf);
      current = current->prev;
    }
    pthread_mutex_unlock(&server_data.list->mutex);
    printf("Logged %s to the chat\n", login);
    int mem = strlen("NEW ") + strlen(client->name) + 1;
    char *buffer = calloc(mem, sizeof(char));
    snprintf(buffer, mem, "NEW %s", client->name);
    broadcast_msg(buffer, client);
    free(buffer);
  }
  return 0;
}

void *accept_connections(void *args) {
  while (true) {
    SA client_info;
    socklen_t info_len;
    int newconnectionfd = accept(server_data.socket, (SA *)&client_info, &info_len);
    if (newconnectionfd < 0) {
      perror("Accept error");
      continue;
    }
    client_t *newclient = calloc(1, sizeof(client_t));
    newclient->socket = newconnectionfd;
    newclient->address = client_info;
    newclient->address_len = info_len;
    pthread_t thread;
    pthread_create(&thread, NULL, handle_new_connection, newclient);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_info, client_ip, INET_ADDRSTRLEN);
    printf("%s connedted\n", client_ip);
  }
  return 0;
}

int main(int argc, char **argv) {
  int port = PORT;
  if (argc == 2) {
    port = atoi(argv[1]);
    if (port < 1) {
      printf("usage: %s <PORT>\n%s is not a valid port", argv[0], argv[1]);
      exit(0);
    }
  }

  server_data.cores = sysconf(_SC_NPROCESSORS_ONLN);
  printf("Cores detected: %d\n", server_data.cores);

  if (server_data.cores > 1) {
    //if there's more than one core
    //the server will use one core to listen for new connections only
    server_data.cores -= 1;
  }

  printf("Max users possible: %d\n", CLIENTS_PER_THREAD * server_data.cores);
  server_data.socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_data.socket < 0) {
    perror("Socket error");
    exit(0);
  }

  int val = 1;
  int err = setsockopt(server_data.socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  if (err < 0) {
    perror("Coudln't make the server socket reusable");
    exit(0);
  }

  SA_IN address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);

  err = bind(server_data.socket, (SA *)&address, sizeof(address));
  if (err < 0) {
    perror("Binding error");
    exit(0);
  }

  err = listen(server_data.socket, MAX_CONNECTIONS);
  if (err < 0) {
    perror("Listen error");
    exit(0);
  }

  server_data.list = lcreate();
  server_data.workers = calloc(server_data.cores, sizeof(worker_t));
  for (int i = 0; i < server_data.cores; i++) {
    server_data.workers[i].saved_fds = 0;
    server_data.workers[i].nfds = FDS_PER_THREAD;
    pipe(server_data.workers[i].pipeptr);
    for (int j = 0; j < FDS_PER_THREAD; j++) {
      server_data.workers[i].fds[j].fd = j == 0 ? server_data.workers[i].pipeptr[0] : VACANT_FD;
      server_data.workers[i].fds[j].events = POLLIN | POLLHUP;
    }
    pthread_create(&server_data.workers[i].thread, NULL, watch_sockets, server_data.workers + i);
    pthread_mutex_init(&server_data.workers[i].mutex, NULL);
    pthread_mutex_init(&server_data.workers[i].pipe_mutex, NULL);
  }

  pthread_create(&server_data.listening_thread, NULL, accept_connections, NULL);
  printf("Server is listening to connections on port %d, press e to stop\n", port);
  char key = 0;
  while (key != 'e') {
    key = getchar();
  }
  server_cleanup();
  return 0;
}