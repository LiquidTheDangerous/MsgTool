#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#define NUM_LISTEN 5

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

pthread_mutex_t connections_mutex;

typedef struct client_connection {
    int sock_fd;
    sockaddr_in address;
    struct client_connection *next;
} client_connection;

void format_buff(char *buff, const char *fmtstr, ...) {
    va_list args;
    va_start(args, fmtstr);
    vsprintf(buff, fmtstr, args);
    va_end(args);
}

void remove_all_lf(char *string) {
    while (*string != '\0') {
        if (*string == '\n') {
            *string = '\0';
            break;
        }
        ++string;
    }
}

void add_lf_to_end(char *string) {
    while (*string != '\0') {
        ++string;
    }
    *string = '\n';
    string[1] = '\0';
}


client_connection *create_connection(int sock_fd, sockaddr_in address) {
    client_connection *result = malloc(sizeof(client_connection));
    result->address = address;
    result->sock_fd = sock_fd;
    result->next = NULL;
    return result;
}

void push_connection(client_connection **list, client_connection *new_connection) {
    pthread_mutex_lock(&connections_mutex);
    new_connection->next = *list;
    *list = new_connection;
    pthread_mutex_unlock(&connections_mutex);
}

void remove_connection_by_sockfd(client_connection **list, int sock_fd) {
    pthread_mutex_lock(&connections_mutex);
    client_connection *current = *list;
    if (current->sock_fd == sock_fd) {
        *list = current->next;
        free(current);
        return;
    }
    while (current != NULL &&
           current->next != NULL &&
           current->next->sock_fd != sock_fd) {
        current = current->next;
    }
    if (current->next != NULL) {
        client_connection *to_remove = current->next;
        current->next = to_remove->next;
        free(to_remove);
    }
    pthread_mutex_unlock(&connections_mutex);
}

client_connection *connections;


void *process_connection(void *param) {
    client_connection *connection = param;
    int buffer_size = 1 << 10;
    char buffer[buffer_size];
    char user_name_buffer[1 << 10];

    int read_count = recv(connection->sock_fd, buffer, buffer_size, 0);
    if (read_count <= 0) {
        goto exit;
    }
    strcpy(user_name_buffer, buffer);
    remove_all_lf(user_name_buffer);

    printf("connected user name: %s\n", user_name_buffer);
    fflush(stdout);
    format_buff(buffer, "Hello, %s!\n", user_name_buffer);
    write(connection->sock_fd, buffer, 1 << 10);

    while (recv(connection->sock_fd, buffer, 1 << 10, 0) > 0) {
        client_connection *current;
        char msg_tmp[1 << 10];
        remove_all_lf(buffer);
        strcpy(msg_tmp, buffer);
        format_buff(buffer, "%s: %s", user_name_buffer, msg_tmp);
        add_lf_to_end(buffer);

        for (current = connections; current != NULL; current = current->next) {
            if (current->sock_fd == connection->sock_fd) {
                continue;
            }
            write(current->sock_fd, buffer, 1 << 10);
        }

        bzero(buffer, 1 << 10);
    }


    exit:
    printf("user %s leave chat", user_name_buffer);
    close(connection->sock_fd);
    remove_connection_by_sockfd(&connections, connection->sock_fd);
    return NULL;
}


void configure_server_address(sockaddr_in *addr, uint16_t port) {
    addr->sin_port = htons(port);
    addr->sin_family = AF_INET; //ipv4
    addr->sin_addr.s_addr = INADDR_ANY; // 0.0.0.0/2
}

void configure_server_socket(int *sock_fd) {
    *sock_fd = socket(AF_INET, SOCK_STREAM, 0);
}


int main() {
    connections = NULL;
    pthread_mutex_init(&connections_mutex, NULL);

    sockaddr_in server_address;
    int server_socket_fd;
    configure_server_address(&server_address, 1234);
    configure_server_socket(&server_socket_fd);
    int bind_result = bind(server_socket_fd, (sockaddr *) (&server_address), sizeof(sockaddr_in));
    if (bind_result == -1) {
        fprintf(stderr, "bind error: %d", bind_result);
        exit(1);
    }

    int listen_result = listen(server_socket_fd, NUM_LISTEN);
    if (listen_result == -1) {
        fprintf(stderr, "listen error: %d", listen_result);
        exit(1);
    }

    for (;;) {
        sockaddr_in client_address;
        int client_socket;
        socklen_t client_addr_len = sizeof(sockaddr_in);
        client_socket = accept(server_socket_fd, (sockaddr *) &client_address, &client_addr_len);

        client_connection *conn = create_connection(client_socket, client_address);
        push_connection(&connections, conn);

        pthread_t thread_d;
        pthread_create(&thread_d, NULL, process_connection, conn);

    }
    pthread_mutex_destroy(&connections_mutex);

    return 0;
}