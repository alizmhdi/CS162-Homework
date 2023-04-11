#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

#define MAX_SIZE 16384


void send_to_client(int dst, int src) {
  void *buffer = malloc(MAX_SIZE);
  size_t size;
  while ((size = read(src, buffer, MAX_SIZE)) > 0)
    http_send_data(dst, buffer, size);

  free(buffer);
}


/*
 * Serves the contents the file stored at `path` to the client socket `fd`.
 * It is the caller's reponsibility to ensure that the file stored at `path` exists.
 * You can change these functions to anything you want.
 * 
 * ATTENTION: Be careful to optimize your code. Judge is
 *            sesnsitive to time-out errors.
 */
void serve_file(int fd, char *path, struct stat *st) {
  long size = st->st_size;
  char *content_size = malloc(MAX_SIZE * sizeof(char));
  sprintf(content_size, "%ld", size);

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(path));
  http_send_header(fd, "Content-Length", content_size);
  http_end_headers(fd);

  int file = open(path, 0);
  send_to_client(fd, file);

  close(file);
  free(content_size);
}


void send_404_not_found(int fd) {
  http_start_response(fd, 404);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);
}


void serve_directory(int fd, char *path) {
  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(".html"));
  http_end_headers(fd);

  DIR *dir = opendir(path);
  if (dir) {
    char *string = malloc(MAX_SIZE);
    struct dirent *dirent;
    while ((dirent = readdir(dir)) != NULL) {
      snprintf(string, MAX_SIZE, "<a href='./%s'>%s</a><br>\n", dirent->d_name, dirent->d_name);
      http_send_string(fd, string);
    }
    free(string);
  }
  closedir(dir);
}


/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 * 
 *   Closes the client socket (fd) when finished.
 */
void handle_files_request(int fd) {

  struct http_request *request = http_request_parse(fd);

  if (request == NULL || request->path[0] != '/') {
    http_start_response(fd, 400);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    close(fd);
    return;
  }

  if (strstr(request->path, "..") != NULL) {
    http_start_response(fd, 403);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    close(fd);
    return;
  }

  char *path = malloc(strlen(server_files_directory) + strlen(request->path));
  strcpy(path, server_files_directory);
  strcat(path, request->path);

  struct stat file_stat;
  stat(path, &file_stat);

  if (S_ISREG(file_stat.st_mode)) {
    serve_file(fd, path, &file_stat);
  } else if (S_ISDIR(file_stat.st_mode)) {
    char *index_path = malloc(strlen(path) + strlen("/index.html"));
    strcpy(index_path, path);
    strcat(index_path, "/index.html");
    if (stat(index_path, &file_stat) == 0)
      serve_file(fd, index_path, &file_stat);

    else
      serve_directory(fd, path);

    free(index_path);
  } else
    send_404_not_found(fd);

  free(path);
  free(request);
  close(fd);
  return;
}


typedef struct proxy_status {
  int src_socket;
  int dst_socket;
  pthread_cond_t *cond;
  int alive;
} proxy_status;


struct proxy_status * create_proxy_status(int src, int dst, int alive, pthread_cond_t *cond) {
  proxy_status *proxy_status = (struct proxy_status*) malloc(sizeof(proxy_status));
  proxy_status->src_socket = src;
  proxy_status->dst_socket = dst;
  proxy_status->alive = alive;
  proxy_status->cond = cond;

  return proxy_status;
}


void *run_proxy(void *args) {
  proxy_status *pstatus = (proxy_status *) args;
  send_to_client(pstatus->dst_socket, pstatus->src_socket);
  pstatus->alive = 0;
  pthread_cond_signal(pstatus->cond);
}


void send_502_bad_gateway(int fd, int target_fd) {
  http_request_parse(fd);
  http_start_response(fd, 502);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);
  http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
  close(target_fd);
  close(fd);
}


void handle_proxy(int fd, int target_fd) {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

  proxy_status *request_status = create_proxy_status(fd, target_fd, 1, &cond);
  proxy_status *response_status = create_proxy_status(target_fd, fd, 1, &cond);

  pthread_t request_thread, response_thread;

  pthread_create(&request_thread, NULL, run_proxy, request_status);
  pthread_create(&response_thread, NULL, run_proxy, response_status);

  while (request_status->alive && response_status->alive)
    pthread_cond_wait(&cond, &mutex);

  pthread_cancel(request_thread);
  pthread_cancel(response_thread);
  free(request_status);
  free(response_status);
  close(fd);
  close(target_fd);

  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&cond);
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int target_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (target_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    close(fd);
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    close(target_fd);
    close(fd);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(target_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0)
    send_502_bad_gateway(fd, target_fd);
  else
    handle_proxy(fd, target_fd);
}


void * thread_handler(void *args) {
  void (*func)(int) = args;
  while (1) {
    int fd = wq_pop(&work_queue);
    func(fd);
    close(fd);
  }
}


void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  pthread_t pthread[num_threads];
  for (int i = 0; i < num_threads; i++)
    pthread_create(&pthread[i], NULL, thread_handler, request_handler);
}


/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  wq_init(&work_queue);
  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    if (num_threads == 0) {
      wq_push(&work_queue, client_socket_number);
    } else {
      request_handler(client_socket_number);
      close(client_socket_number);
    }
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}


int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}


int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);
  signal(SIGPIPE, SIG_IGN);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
