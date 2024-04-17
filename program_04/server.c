// EECE-446-SP-2024
// David Cathers & Maddison Webb

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdbool.h>

#define SERVER_PORT "5432"
#define MAX_LINE 256
#define MAX_PENDING 5
#define BUFF_SIZE 2048

const uint8_t VERBOSE = 1;

enum client_state {
    CLIENT_UNKNOWN,
    CLIENT_JOINED,
    CLIENT_REGISTERED,
};

struct client {
    bool active;
    uint32_t id;
    int type;
    int sock;
    struct sockaddr addr;
    size_t files_len;
    char **files;
};

struct server {
    size_t max_conn;
    size_t max_files;
    size_t active_clients;
    struct client *clients;
    int listen_sock;
    fd_set all_socks;
};

void clear_client_files(struct client *c);
void clear_client(struct client *c);

struct client* search_client_files(const char* find, struct client* client);
void handle_join(struct server *server, struct client *client, uint8_t *buffer, size_t len);
void handle_publish(struct server *server, struct client *client, uint8_t *buffer, size_t len);
void handle_search(struct server *server, struct client *client, uint8_t *buffer, size_t len);

/*
 * Create, bind and passive open a socket on a local interface for the provided service.
 * Argument matches the second argument to getaddrinfo(3).
 *
 * Returns a passively opened socket or -1 on error. Caller is responsible for calling
 * accept and closing the socket.
 */
int bind_and_listen(const char *service);

/*
 * Return the maximum socket descriptor set in the argument.
 * This is a helper function that might be useful to you.
 */
int find_max_fd(const fd_set *fs);

int send_bytes(int sock, const uint8_t *buf, size_t len);
void print_buffer(const uint8_t *buf, size_t length, uint8_t flags);

void handle_signal(int signo);

int main(void) {
    // ------------------------------------------------------------------------
    // signal intercepts
    // ------------------------------------------------------------------------
    struct sigaction sig;
    sig.sa_handler = handle_signal;
    sig.sa_flags = 0;
    sigemptyset(&sig.sa_mask);

    if (sigaction(SIGTERM, &sig, NULL) != 0) {
        perror("[server] failed setting SIGTERM handler");
        return 1;
    }

    if (sigaction(SIGINT, &sig, NULL) != 0) {
        perror("[server] failed setting SIGINT handler");
        return 1;
    }

    sigset_t sigset;
    sigset_t oldset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_BLOCK, &sigset, &oldset);

    // ------------------------------------------------------------------------
    // server setup
    // ------------------------------------------------------------------------
    fd_set call_set;

    struct server srv;
    srv.max_conn = 5;
    srv.max_files = 15;
    srv.active_clients = 0;
    srv.clients = calloc(sizeof(struct client), srv.max_conn);

    if (srv.clients == NULL) {
        perror("[server] failed allocation client connection data");

        return 1;
    }

    for (size_t index = 0; index < srv.max_conn; ++index) {
        srv.clients[index].active = 0;
        srv.clients[index].id = 0;
        srv.clients[index].sock = 0;
        srv.clients[index].files_len = 0;
        srv.clients[index].files = NULL;
    }

    FD_ZERO(&srv.all_socks);
    FD_ZERO(&call_set);

    printf("[server] creating listening socket\n");

    srv.listen_sock = bind_and_listen(SERVER_PORT);
    FD_SET(srv.listen_sock, &srv.all_socks);

    int max_socket = srv.listen_sock;
    uint8_t recv_buffer[BUFF_SIZE];

    // ------------------------------------------------------------------------
    // main loop
    // ------------------------------------------------------------------------
    while (1) {
        call_set = srv.all_socks;

        printf("[server] waiting for activity\n");

        int num_s = pselect(max_socket + 1, &call_set, NULL, NULL, NULL, &oldset);

        if (num_s < 0) {
            if (errno == EINTR) {
                printf("[server] signal interupt\n");
                break;
            } else {
                perror("[server] pselect:");
                break;
            }
        }

        for (int s = 3; s <= max_socket; ++s){
            if (!FD_ISSET(s, &call_set)) {
                continue;
            }

            if (s == srv.listen_sock) {
                if (srv.active_clients == srv.max_conn - 1) {
                    printf("[server] max server connections reached\n");
                    continue;
                }

                printf("[server] accepting new connection\n");

                struct sockaddr client_addr;
                socklen_t client_len = sizeof(client_addr);

                int client_sock = accept(srv.listen_sock, &client_addr, &client_len);

                if (client_sock == -1) {
                    perror("[server] failed to accept client");
                    continue;
                }

                printf("[server] socket value: %d\n", client_sock);

                FD_SET(client_sock, &srv.all_socks);

                if (client_sock > max_socket) {
                    max_socket = client_sock;
                }

                for (size_t index = 0; index < srv.max_conn; ++index) {
                    if (!srv.clients[index].active) {
                        srv.clients[index].active = true;
                        srv.clients[index].sock = client_sock;
                        srv.clients[index].addr = client_addr;
                        srv.active_clients += 1;
                        break;
                    }
                }
            } else {
                struct client *curr = NULL;

                for (size_t index = 0; index < srv.max_conn; ++index) {
                    if (!srv.clients[index].active) {
                        continue;
                    }

                    if (srv.clients[index].sock == s) {
                        curr = &srv.clients[index];
                    }
                }

                ssize_t read = recv(s, recv_buffer, BUFF_SIZE, 0);

                if (read <= 0) {
                    if (read == -1) {
                        fprintf(stderr, "[server] client %d error: %s\n", s, strerror(errno));
                    }

                    printf("[server] client: %d closing\n", s);

                    close(s);

                    FD_CLR(s, &srv.all_socks);

                    if (curr != NULL) {
                        clear_client(curr);
                        srv.active_clients -= 1;
                    }

                    continue;
                }

                printf("[server] client %d data:\n", s);

                print_buffer(recv_buffer, (size_t)read, VERBOSE);

                if (curr == NULL) {
                    printf("[server] failed to find client based on socket: %d\n", s);
                    continue;
                }

                switch (recv_buffer[0]) {
                case 0: // JOIN
                    handle_join(&srv, curr, recv_buffer + 1, (size_t)read - 1);
                    break;
                case 1: // PUBLISH
                    handle_publish(&srv, curr, recv_buffer + 1, (size_t)read - 1);
                    break;
                case 2: // SEARCH
                    handle_search(&srv, curr, recv_buffer + 1, (size_t)read - 1);
                    break;
                default:
                    printf("[server] unknown command received from client: %u\n", recv_buffer[0]);
                    break;
                }
            }
        }
    }

    printf("[server] closing active sockets\n");

    for (size_t index = 0; index < srv.max_conn; ++index) {
        if (!srv.clients[index].active) {
            continue;
        }

        close(srv.clients[index].sock);

        clear_client(&srv.clients[index]);
    }

    free(srv.clients);

    return 0;
}

void handle_signal(int signo) {
    printf("[server] received signal %d\n", signo);
}

void clear_client_files(struct client *c) {
    for (size_t index = 0; index < c->files_len; ++index) {
        free(c->files[index]);
    }

    free(c->files);
}

void clear_client(struct client *c) {
    clear_client_files(c);

    c->active = false;
    c->id = 0;
    c->sock = 0;
    c->files_len = 0;
}

void handle_join(struct server *server, struct client *client, uint8_t *buffer, size_t len) {
    if (len < 4) {
        printf("[server] handle_join: bytes received is less than 4\n");
        return;
    }

    uint32_t received_id = 0;

    memcpy(&received_id, buffer, 4);
    received_id = ntohl(received_id);

    printf("[server] handle_join: client joining registry. id: %u\n", received_id);

    for (size_t index = 0; index < server->max_conn; ++index) {
        if (!server->clients[index].active) {
            continue;
        }

        if (server->clients[index].id == received_id) {
            if (server->clients[index].sock != client->sock) {
                if (client->type == CLIENT_JOINED) {
                    printf("[server] handle_join: client already registered\n");
                } else {
                    printf("[server] handle_join: WARNING client has been REGISTERED\n");
                }
            } else {
                printf("[server] handle_join: client id already registered\n");
            }

            break;
        } else {
            printf("[server] handle_join: client id registered\n");

            client->id = received_id;
            client->type = CLIENT_JOINED;

            break;
        }
    }
}

void handle_publish(struct server *server, struct client *client, uint8_t *buffer, size_t len) {
    if (client->type == CLIENT_UNKNOWN) {
        printf("[server] handle_publish: client has not joined or registered\n");
        return;
    }

    if (len >= 1199) {
        printf("[server] handle_publish: bytes received is greater than 1200\n");
        return;
    }

    printf("[server] handle_publish: client publishing files\n");

    size_t files_len = 0;

    if (len < 4) {
        printf("[server] handle_publish: too few bytes received\n");
        return;
    }

    memcpy(&files_len, buffer, 4);
    files_len = ntohl(files_len);

    if (files_len > server->max_files) {
        printf("[server] handle_publish: number of files is greater than max. given: %lu\n", files_len);
        return;
    }

    bool clean_up = false;
    size_t allocated = 0;
    uint8_t *p = buffer + 4;
    char **files = calloc(sizeof(char *), files_len);

    len -= 4;

    for (size_t count = 0; count < files_len; ++count) {
        bool found_null = false;
        size_t str_len = 0;

        printf("[server] handle_publish: len? %lu\n", len);

        for (; str_len < len; ++str_len) {
            if (p[str_len] == 0) {
                found_null = true;
                break;
            } else if (p[str_len] >= 128) {
                printf("[server] handle_publish: invalid ASCII character received from client\n");

                clean_up = true;

                break;
            }
        }

        if (!found_null) {
            printf("[server] handle_publish: non null terminated string given by client\n");

            clean_up = true;

            break;
        }

        char *str = calloc(sizeof(char), str_len);

        if (str == NULL) {
            printf("[server] handle_publish: failed allocating string\n");

            clean_up = true;

            break;
        }

        strncpy(str, (char *)p, str_len);

        files[allocated] = str;
        allocated += 1;

        printf("[server] handle_publish: str: \"%s\" %lu\n", str, str_len);

        p += str_len + 1;
        len -= str_len + 1;
    }

    if (clean_up) {
        printf("[server] handle_publish: cleaning up allocated strings\n");

        for (size_t index = 0; index < allocated; ++index) {
            free(files[index]);
        }

        free(files);
    } else {
        clear_client_files(client);

        client->files_len = files_len;
        client->files = files;

        printf("[server] handle_publish: client published %u\n", client->id);

        for (size_t index = 0; index < client->files_len; ++index) {
            printf("    %s\n", client->files[index]);
        }
    }
}

struct client* search_client_files(const char *find, struct client *client) {
    for (size_t file_index = 0; file_index < client->files_len; ++file_index) {
        bool invalid = false;
        bool reached_end = false;
        size_t index = 0;

        while (1) {
            if (client->files[file_index][index] == 0) {
                reached_end = true;
                break;
            }

            if (find[index] == 0) {
                break;
            }

            if (find[index] != client->files[file_index][index]) {
                invalid = true;
                break;
            }

            index += 1;
        }

        if (!invalid && reached_end) {
            return client;
        }
    }

    return NULL;
}

void handle_search(struct server *server, struct client *client, uint8_t *buffer, size_t len) {
    if (client->type == CLIENT_UNKNOWN) {
        printf("[server] handle_publish: client has not joined or registered\n");
        return;
    }

    if (len >= 100) {
        printf("[server] handle_search: received too many bytes from client\n");
        return;
    }

    printf("[server] handle_search: client searching files\n");

    uint8_t response[10] = {0};

    struct client *found = NULL;
    char *p = (char *)buffer;

    for (size_t check = 0; check < len; ++check) {
        if (buffer[check] >= 128) {
            printf("[server] handle_search: file name contains non ASCII characters\n");

            if (send_bytes(client->sock, response, 10) != 0) {
                perror("[server] handle_search: error sending resposne");
                return;
            }
        }
    }

    if (buffer[len - 1] != 0) {
        printf("[server] handle_search: non null terminated string from client\n");

        if (send_bytes(client->sock, response, 10) != 0) {
            perror("[server] handle_search: error sending resposne");
            return;
        }
    }

    for (size_t index = 0; index < server->max_conn; ++index) {
        if (!server->clients[index].active) {
            continue;
        }

        printf("[server] handle_search: checking client: %u\n", server->clients[index].id);

        found = search_client_files(p, &server->clients[index]);

        if (found != NULL) {
            printf("[server] handle_search: found file. id: %u\n", found->id);
            break;
        }
    }

    if (found == NULL) {
        printf("[server] handle_search: failed to find file\n");
    }

    printf("[server] handle_search: sending response\n");

    if (send_bytes(client->sock, response, 10) != 0) {
        perror("[server] handle_search: error sending resposne");
    }
}

int send_bytes(int sock, const uint8_t *buff, size_t len) {
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t sent = send(sock, buff + total_sent, len - total_sent, 0);

        if (sent < 0) {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

void print_buffer(const uint8_t *buff, size_t length, uint8_t flags) {
    printf("buffer:");

    for (size_t index = 0; index < length; ++index) {
        if (buff[index] <= 0x0f) {
            printf(" 0%x", buff[index]);
        } else {
            printf(" %x", buff[index]);
        }
    }

    if ((flags & VERBOSE) == VERBOSE) {
        printf("\n      :");

        for (size_t index = 0; index < length; ++index) {
            if (buff[index] == '\n') {
                printf(" \\n");
            } else if (buff[index] < 32) {
                printf(" CC");
            } else {
                printf("  %c", buff[index]);
            }
        }

        printf("\n");
    } else {
        printf("\n");
    }
}

int find_max_fd(const fd_set *fs) {
    int ret = 0;

    for (int i = FD_SETSIZE - 1; i >= 0 && ret == 0; --i) {
        if (FD_ISSET(i, fs)){
            ret = i;
        }
    }

    return ret;
}

int bind_and_listen(const char *service) {
    struct addrinfo hints;
    struct addrinfo *rp, *result;
    int s;

    /* Build address data structure */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    /* Get local address info */
    if ((s = getaddrinfo(NULL, service, &hints, &result)) != 0) {
        fprintf(stderr, "[server] bind_and_listen: getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    /* Iterate through the address list and try to perform passive open */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if ((s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1) {
            continue;
        }

        if (!bind(s, rp->ai_addr, rp->ai_addrlen)) {
            perror("[server] bind_and_listen: bind");
            break;
        }

        close(s);
    }

    if (rp == NULL) {
        return -1;
    }

    if (listen(s, MAX_PENDING) == -1) {
        perror("[server] bind_and_listen: listen");
        close(s);
        return -1;
    }

    freeaddrinfo(result);

    return s;
}
