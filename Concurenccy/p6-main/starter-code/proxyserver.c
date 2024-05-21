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

#include "proxyserver.h"
#include "safequeue.h"
/*
 * Constants
 */
#define RESPONSE_BUFSIZE 10000

/*
 * Global configuration variables.
 * Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int num_listener;
int *listener_ports;
int num_workers;
char *fileserver_ipaddr;
int fileserver_port;
int max_queue_size;
// Added
struct targ_struct{
    int portIndex;
    int number;
    int fd;
};
pthread_mutex_t queueLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queueCondition = PTHREAD_COND_INITIALIZER;

int queueSize = 0;

PQueue *pqueue;

void send_error_response(int client_fd, status_code_t err_code, char *err_msg) {
    http_start_response(client_fd, err_code);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    char *buf = malloc(strlen(err_msg) + 2);
    sprintf(buf, "%s\n", err_msg);
    http_send_string(client_fd, buf);
    return;
}

// essentially a worker thread
/*
 * forward the client request to the fileserver and
 * forward the fileserver response to the client
 */
void serve_request(struct http_request req) {
    int client_fd = req.client_fd;
    // create a fileserver socket
    int fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fileserver_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        exit(errno);
    }

    // create the full fileserver address
    struct sockaddr_in fileserver_address;
    fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
    fileserver_address.sin_family = AF_INET;
    fileserver_address.sin_port = htons(fileserver_port);

    // connect to the fileserver
    int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                    sizeof(fileserver_address));
    if (connection_status < 0) {
        // failed to connect to the fileserver
        printf("Failed to connect to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }

    // successfully connected to the file server
    char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));

    // forward the client request to the fileserver
    int bytes_read = read(client_fd, buffer, RESPONSE_BUFSIZE);

    int ret = http_send_data(fileserver_fd, buffer, bytes_read);
    if (ret < 0) {
        printf("Failed to send request to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");

    } else {
        // forward the fileserver response to the client
        while (1) {
            int bytes_read = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
            if (bytes_read <= 0) // fileserver_fd has been closed, break
                break;
            // printf("receive| %s\n", buffer);
            ret = http_send_data(client_fd, buffer, bytes_read);
            // printf("returned| %s\n", buffer);
            if (ret < 0) { // write failed, client_fd has been closed
                break;
            }
        }
    }

    // close the connection to the fileserver
    shutdown(fileserver_fd, SHUT_WR);
    close(fileserver_fd);

    // close the connection to client //added 
    shutdown(client_fd, SHUT_WR);
    close(client_fd);

    // Free resources and exit
    free(buffer);
}

void *worker_thread(void *args) {
    while (1) {
        // Acquire lock
        pthread_mutex_lock(&queueLock);
        
        // Wait if queue is empty
        while (queueSize == 0) {
            pthread_cond_wait(&queueCondition, &queueLock);
        }
        // Pop from queue
        struct http_request req = get_work(pqueue);
        queueSize = pqueue->size;

        pthread_mutex_unlock(&queueLock);

        // Check for delays and wait
        if (req.delay > 0) {
            // Go to sleep
            sleep(req.delay);
        }

        // Process request
        serve_request(req);
    }
    return NULL;
}

int server_fd;
/*
 * opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *server_fd, int *port) {

    // create a socket to listen
    *server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (*server_fd == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }

    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }

    int proxy_port = *port;
    // create the full address of this proxyserver
    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET;
    proxy_address.sin_addr.s_addr = INADDR_ANY;
    proxy_address.sin_port = htons(proxy_port); // listening port

    // bind the socket to the address and port number specified in
    if (bind(*server_fd, (struct sockaddr *)&proxy_address,
             sizeof(proxy_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }

    // starts waiting for the client to request a connection
    if (listen(*server_fd, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    printf("Listening on port %d...\n", proxy_port);

    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_fd;
    while (1) {
        client_fd = accept(*server_fd,
                           (struct sockaddr *)&client_address,
                           (socklen_t *)&client_address_length);
        if (client_fd < 0) {
            perror("Error accepting socket");
            continue;
        }

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               client_address.sin_port);

        // Added
        struct http_request req;
        parse_client_request(client_fd, &req);

        if (strcmp(req.path, GETJOBCMD) == 0) {
            // Pop from queue and give response
            // Pop queue
            pthread_mutex_lock(&queueLock);
            if (queueSize == 0) {
                http_send_response(client_fd, QUEUE_EMPTY, "");
            } else {
                struct http_request res = get_work(pqueue);
                queueSize = pqueue->size;

                shutdown(res.client_fd, SHUT_WR);
                close(res.client_fd);

                // Response
                int bufferSize = strlen(res.path) + 5;
                char buffer[bufferSize];
                sprintf(buffer, "%s\r\f", res.path);
                http_send_response(client_fd, OK, buffer);
            }
            pthread_mutex_unlock(&queueLock);
            // close the connection to the client
            shutdown(client_fd, SHUT_WR);
            close(client_fd);

        } else {
            pthread_mutex_lock(&queueLock);

            // Check if queue full
            if (queueSize == max_queue_size) {
                http_send_response(client_fd, QUEUE_FULL, "");
                shutdown(client_fd, SHUT_WR);
                close(client_fd);
            } else {
                // Push to queue
                add_work(pqueue, req);
                queueSize = pqueue->size;
                // Signal
                pthread_cond_signal(&queueCondition);
            }
            pthread_mutex_unlock(&queueLock);


        }
    }

    shutdown(*server_fd, SHUT_RDWR);
    close(*server_fd);
}

// Added
void *listener_thread(void *targs) {
    struct targ_struct *targsptr = (struct targ_struct *) targs;

    int port = listener_ports[targsptr->portIndex];
    serve_forever(&targsptr->fd, &port);

    // Free resources
    free(targs);
    return (void *) 0;
}

// Added
void thread_main() {
    // Thread resources
    int total_threads = num_listener + num_workers;
    pthread_t thread_id[total_threads];
    struct targ_struct** threadarg_arr = 
        (struct targ_struct**) malloc(num_listener * sizeof(struct targ_struct));

    // Threading
    // Listener Threads
    for (int i = 0; i < num_listener; i++) {
        threadarg_arr[i] = (struct targ_struct*) malloc(sizeof(struct targ_struct));

        // Intitalize listener thread arguments
        threadarg_arr[i]->portIndex = i;

        int res = pthread_create(&thread_id[i], NULL, 
            listener_thread, (void *) threadarg_arr[i]);

        // Check listener thread creation
        if (res != 0) {
            perror("thread create failed");
            exit(1);
        }
    }

    // Worker Threads
    for (int i = 0; i < num_workers; i++) {
        // printf("creating worker %d\n", i);
        int res = pthread_create(&thread_id[num_listener + i], NULL, 
            worker_thread, NULL);

        // Check worker thread creation
        if (res != 0) {
            perror("thread create failed");
            exit(1);
        }
    }

    // Wait for all threads
    for (int i = 0; i < total_threads; i++) {
        pthread_join(thread_id[i], NULL);
    }

    // Free resources
    for (int i = 0; i < num_listener; i++) {
        free(threadarg_arr[i]);
    }
    free(threadarg_arr);
}


/*
 * Default settings for in the global configuration variables
 */
void default_settings() {
    num_listener = 1;
    listener_ports = (int *)malloc(num_listener * sizeof(int));
    listener_ports[0] = 8000;

    num_workers = 1;

    fileserver_ipaddr = "127.0.0.1";
    fileserver_port = 3333;

    max_queue_size = 100;
}

void print_settings() {
    printf("\t---- Setting ----\n");
    printf("\t%d listeners [", num_listener);
    for (int i = 0; i < num_listener; i++)
        printf(" %d", listener_ports[i]);
    printf(" ]\n");
    printf("\t%d workers\n", num_listener);
    printf("\tfileserver ipaddr %s port %d\n", fileserver_ipaddr, fileserver_port);
    printf("\tmax queue size  %d\n", max_queue_size);
    printf("\t  ----\t----\t\n");
}

void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    for (int i = 0; i < num_listener; i++) {
        if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    }
    free(listener_ports);
    exit(0);
}

char *USAGE =
    "Usage: ./proxyserver [-l 1 8000] [-n 1] [-i 127.0.0.1 -p 3333] [-q 100]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_callback_handler);

    /* Default settings */
    default_settings();

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp("-l", argv[i]) == 0) {
            num_listener = atoi(argv[++i]);
            free(listener_ports);
            listener_ports = (int *)malloc(num_listener * sizeof(int));
            for (int j = 0; j < num_listener; j++) {
                listener_ports[j] = atoi(argv[++i]);
            }
        } else if (strcmp("-w", argv[i]) == 0) {
            num_workers = atoi(argv[++i]);
        } else if (strcmp("-q", argv[i]) == 0) {
            max_queue_size = atoi(argv[++i]);
        } else if (strcmp("-i", argv[i]) == 0) {
            fileserver_ipaddr = argv[++i];
        } else if (strcmp("-p", argv[i]) == 0) {
            fileserver_port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }
    print_settings();

    // Added
    // Initialize priority queue
    pqueue = create_queue(max_queue_size);

    thread_main();

    return EXIT_SUCCESS;
}

// Everything under is added
/**
 * Definitions from proxyserver.h
*/
void http_start_response(int fd, int status_code) {
    dprintf(fd, "HTTP/1.0 %d %s\r\n", status_code,
            http_get_response_message(status_code));
}

void http_send_header(int fd, char *key, char *value) {
    dprintf(fd, "%s: %s\r\n", key, value);
}

void http_end_headers(int fd) {
    dprintf(fd, "\r\n");
}

void http_send_string(int fd, char *data) {
    http_send_data(fd, data, strlen(data));
}

void http_send_response(int fd, int status_code, char *content) {

    char *status_code_msg = http_get_response_message(status_code);
    int bufferLength = 5 + strlen(status_code_msg) + 44 + strlen(content);
    char buffer[bufferLength];
    sprintf(buffer, "HTTP/1.0 %d %s\r\nContent-Type: text/plain\r\n\r\n%s",
        status_code, status_code_msg, content);
    http_send_string(fd, buffer);
}

int http_send_data(int fd, char *data, size_t size) {
    ssize_t bytes_sent;
    while (size > 0) {
        bytes_sent = write(fd, data, size);
        if (bytes_sent < 0)
            return -1; // Indicates a failure
        size -= bytes_sent;
        data += bytes_sent;
    }
    return 0; // Indicate success
}

void http_fatal_error(char *message) {
    fprintf(stderr, "%s\n", message);
    exit(ENOBUFS);
}



struct http_request *http_request_parse(int fd) {
    struct http_request *request = malloc(sizeof(struct http_request));
    if (!request) http_fatal_error("Malloc failed");

    char *read_buffer = malloc(LIBHTTP_REQUEST_MAX_SIZE + 1);
    if (!read_buffer) http_fatal_error("Malloc failed");

    int bytes_read = read(fd, read_buffer, LIBHTTP_REQUEST_MAX_SIZE);
    read_buffer[bytes_read] = '\0'; /* Always null-terminate. */

    char *read_start, *read_end;
    size_t read_size;

    do {
        /* Read in the HTTP method: "[A-Z]*" */
        read_start = read_end = read_buffer;
        while (*read_end >= 'A' && *read_end <= 'Z') {
            printf("%c", *read_end);
            read_end++;
        }
        read_size = read_end - read_start;
        if (read_size == 0) break;
        request->method = malloc(read_size + 1);
        memcpy(request->method, read_start, read_size);
        request->method[read_size] = '\0';
        printf("parsed method %s\n", request->method);

        /* Read in a space character. */
        read_start = read_end;
        if (*read_end != ' ') break;
        read_end++;

        /* Read in the path: "[^ \n]*" */
        read_start = read_end;
        while (*read_end != '\0' && *read_end != ' ' && *read_end != '\n')
            read_end++;
        read_size = read_end - read_start;
        if (read_size == 0) break;
        request->path = malloc(read_size + 1);
        memcpy(request->path, read_start, read_size);
        request->path[read_size] = '\0';
        printf("parsed path %s\n", request->path);

        /* Read in HTTP version and rest of request line: ".*" */
        read_start = read_end;
        while (*read_end != '\0' && *read_end != '\n')
            read_end++;
        if (*read_end != '\n') break;
        read_end++;

        free(read_buffer);
        return request;
    } while (0);

    /* An error occurred. */
    free(request);
    free(read_buffer);
    return NULL;
}

char *http_get_response_message(int status_code) {
    switch (status_code) {
    case 100:
        return "Continue";
    case 200:
        return "OK";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    default:
        return "Internal Server Error";
    }
}

void parse_client_request(int fd, struct http_request *ptr) {
    char *read_buffer = malloc(LIBHTTP_REQUEST_MAX_SIZE + 1);
    if (!read_buffer) http_fatal_error("Malloc failed");

    int bytes_read = recv(fd, read_buffer, LIBHTTP_REQUEST_MAX_SIZE - 1, MSG_PEEK);
    read_buffer[bytes_read] = '\0'; /* Always null-terminate. */

    int delay = -1;
    int priority = -1;
    char *path = NULL;

    int is_first = 1;
    size_t size;

    char *token = strtok(read_buffer, "\r\n");
    while (token != NULL) {
        size = strlen(token);
        if (is_first) {
            is_first = 0;
            // get path
            char *s1 = strstr(token, " ");
            char *s2 = strstr(s1 + 1, " ");
            size = s2 - s1 - 1;
            path = strndup(s1 + 1, size);

            if (strcmp(GETJOBCMD, path) == 0) {
                break;
            } else {
                // get priority
                s1 = strstr(path, "/");
                s2 = strstr(s1 + 1, "/");
                size = s2 - s1 - 1;
                char *p = strndup(s1 + 1, size);
                priority = atoi(p);
            }
        } else {
            char *value = strstr(token, ":");
            if (value) {
                size = value - token - 1;  // -1 for space
                if (strncmp("Delay", token, size) == 0) {
                    delay = atoi(value + 2);  // skip `: `
                }
            }
        }
        token = strtok(NULL, "\r\n");
    }
    
    // Added
    ptr->path = path;
    ptr->priority = priority;
    ptr->delay = delay;
    ptr->client_fd = fd;
    free(read_buffer);
    return;
}

