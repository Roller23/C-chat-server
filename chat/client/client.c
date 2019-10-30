#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <ncurses.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>

typedef struct sockaddr SA;
typedef struct sockaddr_in SA_IN;

#define NAME_LEN 20
#define BUFFER_SIZE 4096
#define MAX_MESSAGE 8192
#define MSG_SIZE 2048

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct {
  int width, height;
  int input_start;
  int messages_width;
  int max_rows;
  int max_lines;
  char **messages;
  char **users;
  WINDOW *chatbox, *onlinelist, *msgbox;
} chat;

struct tm *timestamp(void) {
  time_t now = time(0);
  return localtime(&now);
}

bool starts_with(char *str1, char *str2) {
  return strncmp(str1, str2, strlen(str2) - 1) == 0;
}

void cleanup(void) {
  for (int i = 0; i < chat.max_lines; i++) {
    if (chat.messages[i]) {
      free(chat.messages[i]);
    }
    if (chat.users[i]) {
      free(chat.users[i]);
    }
  }
  free(chat.messages);
  free(chat.users);
}

void add_message(char *msg) {
  pthread_mutex_lock(&mutex);
  if (chat.messages[chat.max_lines - 1] != NULL) {
    free(chat.messages[chat.max_lines - 1]);
  }
  for (int i = chat.max_lines - 2; i >= 0; i--) {
    chat.messages[i + 1] = chat.messages[i];
  }
  chat.messages[0] = msg;
  pthread_mutex_unlock(&mutex);
}

void add_user(char *user) {
  pthread_mutex_lock(&mutex);
  if (chat.users[chat.max_lines - 1] != NULL) {
    free(chat.users[chat.max_lines - 1]);
  }
  for (int i = chat.max_lines - 2; i >= 0; i--) {
    chat.users[i + 1] = chat.users[i]; 
  }
  chat.users[0] = user;
  pthread_mutex_unlock(&mutex);
}

void remove_user(char *user) {
  pthread_mutex_lock(&mutex);
  int index = 0;
  while (strcmp(chat.users[index], user) != 0) {
    index++;
  }
  free(chat.users[index]);
  int start = 1;
  for (int i = index; i < chat.max_lines - 1; i++) {
    chat.users[i] = chat.users[i + 1];
    if (start) {
      start = 0;
      chat.users[i + 1] = NULL;
    }
  }
  pthread_mutex_unlock(&mutex);
}

int send_msg(int serverfd, const char *msg) {
  int datalen = strlen(msg);
  int data = htonl(datalen);
  int w = write(serverfd, &data, sizeof(data));
  if (w <= 0) {
    return w;
  }
  return write(serverfd, msg, datalen);
}

void refresh_input(void) {
  pthread_mutex_lock(&mutex);
  wclear(chat.chatbox);
  box(chat.chatbox, '|', '-');
  mvwprintw(chat.chatbox, 0, 3, " Type a message... ");
  wrefresh(chat.chatbox);
  pthread_mutex_unlock(&mutex);
}

char *read_msg(int serverfd, int *err) {
  int bufferlen;
  int r = read(serverfd, &bufferlen, sizeof(bufferlen));
  if (r <= 0) {
    if (err) *err = r;
    return NULL;
  }
  bufferlen = ntohl(bufferlen);
  char *newbuf = calloc(bufferlen + 1, sizeof(char));
  int bytes_read = 0;
  while (bufferlen > 0) {
    char *buffer = calloc(bufferlen, sizeof(char));
    int r = read(serverfd, buffer, bufferlen);
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

void *read_input(void *arg) {
  int serverfd = *(int *)arg;
  char message[MSG_SIZE];
  while (true) {
    memset(message, 0, MSG_SIZE);
    mvwscanw(chat.chatbox, 1, 2, "%500[^\n]", message);
    if (strlen(message) == 0) {
      continue;
    }
    if (strcmp(message, "/exit") == 0) {
      break;
    }
    char buffer[MSG_SIZE + 6];
    memset(buffer, 0, MSG_SIZE + 6);
    sprintf(buffer, "MSG %s", message);
    send_msg(serverfd, buffer);
    refresh_input();
  }
  return 0;
}

void *refresh_all(void *arg) {
  while (true) {
    pthread_mutex_lock(&mutex);
    wclear(chat.onlinelist);
    wclear(chat.msgbox);
    box(chat.msgbox, '|', '-');
    box(chat.onlinelist, '|', '-');
    for (int i = chat.max_lines - 1, offset = 1; i >= 0; i--) {
      if (chat.messages[i] == NULL) continue;
      mvwprintw(chat.msgbox, offset, 2, "%s", chat.messages[i]);
      offset++;
    }
    int online = 0;
    for (int i = chat.max_lines - 1, offset = 1; i >= 0; i--) {
      if (chat.users[i] == NULL) continue;
      mvwprintw(chat.onlinelist, offset, 2, "%s", chat.users[i]);
      offset++;
      online++;
    }
    mvwprintw(chat.msgbox, 0, 3, " Messages ");
    mvwprintw(chat.onlinelist, 0, 3, " Online (%d) ", online);
    wrefresh(chat.msgbox);
    wrefresh(chat.onlinelist);
    pthread_mutex_unlock(&mutex);
    usleep(1000 * 100);
  }
}

void *listen_server(void *arg) {
  int serverfd = *(int *)arg;
  while (true) {
    int bytes;
    char *message = read_msg(serverfd, &bytes);
    if (bytes == -1) {
      perror("Message read error");
      break;
    }
    if (bytes == 0) {
      //server poof'd lol
      char str[] = "Lost connection to the server";
      char *buf = calloc(strlen(str) + 1, sizeof(char));
      strcpy(buf, str);
      add_message(buf);
      break;
    }
    if (starts_with(message, "MSG")) {
      char *message_offset = message + strlen("MSG ");
      int mem = strlen(message_offset) + strlen("00:00 ") + 1;
      char *buf = calloc(mem, sizeof(char));
      struct tm *now = timestamp();
      snprintf(buf, mem, "%02d:%02d %s", now->tm_hour, now->tm_min, message_offset);
      int len = strlen(buf);
      int copied = 0;
      while (len > 0) {
        char *line = calloc(chat.messages_width + 1, sizeof(char));
        int tocopy = chat.messages_width - 4;
        strncpy(line, buf + copied, tocopy);
        add_message(line);
        copied += tocopy;
        len -= tocopy;
      }
      free(buf);
    }
    if (starts_with(message, "NEW")) {
      char *message_offset = message + strlen("NEW ");
      int mem = strlen(message_offset) + 1;
      char *buf = calloc(mem, sizeof(char));
      strncpy(buf, message_offset, mem);
      add_user(buf);
    }
    if (starts_with(message, "OUT")) {
      char *message_offset = message + strlen("OUT ");
      int mem = strlen(message_offset) + 1;
      char *buf = calloc(mem, sizeof(char));
      strncpy(buf, message_offset, mem);
      remove_user(buf);
      free(buf);
    }
    free(message);
  }
  return 0;
}

int main(int argc, char **argv) {
  char *server_ip = "127.0.0.1";
  int server_port = 8000;
  char *url = NULL;

  for (int i = 1; i < argc; i++) {
    if (starts_with(argv[i], "url:")) {
      char *ptr = strtok(argv[i], ":");
      ptr = strtok(NULL, ":");
      if (ptr == NULL) {
        printf("No url supplied\n");
        exit(0);
      }
      struct hostent *host = gethostbyname(ptr);
      if (host == NULL) {
        printf("Couldn't find the IP address of %s\n", ptr);
        exit(0);
      }
      server_ip = inet_ntoa(*(struct in_addr *)(host->h_addr_list[0]));
      url = ptr;
    } else if (starts_with(argv[i], "port:")) {
      char *ptr = strtok(argv[i], ":");
      ptr = strtok(NULL, ":");
      if (ptr == NULL) {
        printf("No port supplied\n");
        exit(0);
      }
      server_port = atoi(ptr);
      if (server_port < 1) {
        printf("%s is an invalid port\n", ptr);
        exit(0);
      }
    } else if (starts_with(argv[i], "ip:")) {
      char *ptr = strtok(argv[i], ":");
      ptr = strtok(NULL, ":");
      if (ptr == NULL) {
        printf("No ip supplied");
        exit(0);
      }
      server_ip = ptr;
    }
  }

  char name[21];
  printf("Connecting to %s(%s):%d\n", url ? url : "", server_ip, server_port);
  printf("Enter a nickname: ");
  scanf("%20[a-zA-Z]", name);

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  if (connfd < 0) {
    perror("Socket error");
    exit(0);
  }
  SA_IN server_address;
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(server_port);

  int err = inet_pton(AF_INET, server_ip, &server_address.sin_addr);
  if (err <= 0) {
    perror("IP to binary conversion error");
    exit(0);
  }

  err = connect(connfd, (SA *)&server_address, sizeof(server_address));
  if (err < 0) {
    perror("Connect error");
    exit(0);
  }

  initscr();
  curs_set(FALSE);
  echo();
  getmaxyx(stdscr, chat.height, chat.width);
  chat.input_start = chat.height - 2;
  chat.messages_width = chat.width * 3 / 4;
  chat.max_rows = chat.input_start - 2;
  chat.max_lines = chat.max_rows - 1;
  chat.chatbox = newwin(3, chat.width, chat.height - 3, 0);
  chat.msgbox = newwin(chat.height - 3, chat.messages_width, 0, 0);
  chat.onlinelist = newwin(chat.height - 3, chat.width - chat.messages_width, 0, chat.messages_width);
  refresh_input();

  chat.users = calloc(chat.max_lines, sizeof(char *));
  chat.messages = calloc(chat.max_lines, sizeof(char *));

  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  sprintf(buffer, "LOGIN %s", name);
  write(connfd, buffer, strlen(buffer));
  int bytes;
  char *response = read_msg(connfd, &bytes);
  if (bytes == -1) {
    perror("Response read error");
    cleanup();
    exit(0);
  }
  if (bytes == 0) {
    perror("Lost connection to the server");
    cleanup();
    exit(0);
  }
  if (strcmp(response, "LOGGED") == 0) {
    free(response);
    pthread_t input, listen_thread, refresh_thread;
    pthread_create(&listen_thread, NULL, listen_server, &connfd);
    pthread_create(&input, NULL, read_input, &connfd);
    pthread_create(&refresh_thread, NULL, refresh_all, NULL);
    pthread_join(input, NULL);
  } else {
    printf("Couldn't log in, server response: %s\n", response);
    free(response);
  }
  pthread_mutex_destroy(&mutex);
  cleanup();
  return 0;
}