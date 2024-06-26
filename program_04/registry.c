// EECE-446-SP-2024
// David Cathers & Madison Webb

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_PENDING 5
#define BUFF_SIZE 2048

#define IPLEN_AND_PORT 51

#define TEST_OUTPUT true

const uint8_t VERBOSE = 1;

/**
 * handles incoming signals sent from the system
 */
void handle_signal(int signo);

/**
 * the three different states for a connected client
 */
enum client_state {
    // the client is connected but not join joined or registered
    CLIENT_UNKNOWN,
    // the client has joined
    CLIENT_JOINED,
    // the client has registered
    CLIENT_REGISTERED,
};

/**
 * relevant data we want to store about a connected client
 */
struct client {
    // determines if the current client struct is active or not
    bool active;
    // client id provided by the client
    uint32_t id;
    // type specified by client_state
    int type;
    // socket file descriptor
    int sock;
    // the socketaddr information of the connected client
    struct sockaddr addr;
    // number of files current stored in the server
    size_t files_len;
    // list of file names publish from the client
    char **files;
};

enum server_output {
    STDOUT_LOG,
    FILE_LOG,
};

/**
 * relevant state data we want to store for the server
 */
struct server {
    // max number of active connections the server will handle
    size_t max_conn;
    // max number of files that a client can publish to the server
    size_t max_files;
    // total number of active clients
    size_t active_clients;
    // list of client structs
    struct client *clients;
    // server socket file descriptor
    int listen_sock;
    // all currently connected sockets
    fd_set all_socks;
    // the currently highest socket value
    int max_socket;
    // output type
    int output_type;
    // output stream
    FILE* output;
};

/**
 * free the allocated strings stored for a client
 */
void clear_client_files(struct client *c);

/**
 * resets and frees allocated data for a client
 */
void clear_client(struct client *c);

/**
 * closes the server output if necessary
 */
void close_server_output(struct server* s);

/**
 * accepts a client for the server
 */
void server_accept(struct server* s);

/**
 * attempts to find the desired string for the given client if the string is
 * found then it will return the pointer provided otherwise will return NULL
 */
struct client* search_client_files(struct server* server, const char* find, struct client* client);

/**
 * handles incoming client data
 */
void handle_client(struct server* server, struct client* client);

/**
 * handles a join request sent by a client
 */
void handle_join(struct server *server, struct client *client, uint8_t *buffer, size_t len);

/**
 * handles a publish request sent by a client
 */
void handle_publish(struct server *server, struct client *client, uint8_t *buffer, size_t len);

/**
 * handles a search request sent by a client
 */
void handle_search(struct server *server, struct client *client, uint8_t *buffer, size_t len);

/*
 * Create, bind and passive open a socket on a local interface for the provided service.
 * Argument matches the second argument to getaddrinfo(3).
 *
 * Returns a passively opened socket or -1 on error. Caller is responsible for calling
 * accept and closing the socket.
 */
int bind_and_listen(struct server *server, const char *service);

/**
 * retrieves the ipv4 address and port of the desired sockaddr_in
 */
char* get_ipv4_port(struct sockaddr_in* addr, char* str, size_t len, bool inc_port);

/**
 * retrives the ipv6 address and port of the desired sockaddr_in6
 */
char* get_ipv6_port(struct sockaddr_in6* addr, char* str, size_t len, bool inc_port);

/**
 * retrieves the ip address and port of the desired sockaddr
 */
char* get_ip_port(struct sockaddr* addr, char* str, size_t len, bool inc_port);

/**
 * attempts to send all the desired bytes to the specified socket
 */
int send_bytes(int sock, const uint8_t *buf, size_t len);

/**
 * prints out the given buffer to stdout
 */
void print_buffer(FILE* output, const uint8_t *buf, size_t length, uint8_t flags);

/**
 * logs the given message to the specified output for the server
 */
void srv_log(struct server* server, const char* format, ...);

/**
 * logs the given message to the specified output for the server and will also
 * print the "[INFO]" prefix
 */
void srv_info(struct server* server, const char* format, ...);

/**
 * logs the given message to the specified output for the server and will also
 * print the "[WARN]" prefix
 */
void srv_warn(struct server* server, const char* format, ...);

/**
 * logs the given message to the specified output for the server and will also
 * print the "[ERROR]" prefix
 */
void srv_error(struct server* server, const char* format, ...);

/**
 * logs the given message to the specified output for the server and will also
 * print the "[DEBUG]" prefix
 */
void srv_debug(struct server* server, const char* format, ...);

int main(int argc, char **argv) {
    char *listen_port = "5432";

    static struct option long_options[] = {
        {"listen-port", required_argument, 0, 0},
        {0,0,0,0}
    };

    int option_index = 0;

    while (1) {
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
        case 0:
            switch (option_index) {
            case 0:
                listen_port = optarg;
                break;
            default:
                break;
            }
            break;
        default:
            return 1;
        }
    }

    if (optind < argc) {
        int start = optind;

        listen_port = argv[start];
    }

    // ------------------------------------------------------------------------
    // signal intercepts
    // ------------------------------------------------------------------------
    // we are going to intercept SIGTERM and SIGINT so that we can do proper
    // clean up before terminating the process
    struct sigaction sig;
    sig.sa_handler = handle_signal;
    sig.sa_flags = 0;
    sigemptyset(&sig.sa_mask);

    if (sigaction(SIGTERM, &sig, NULL) != 0) {
        perror("[ERROR] failed setting SIGTERM handler");
        return 1;
    }

    if (sigaction(SIGINT, &sig, NULL) != 0) {
        perror("[ERROR] failed setting SIGINT handler");
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
    srv.max_conn = 50;
    srv.max_files = 10;
    srv.active_clients = 0;
    srv.output_type = STDOUT_LOG;
    srv.output = stdout;

    if (TEST_OUTPUT) {
        char filename[1024];
        uint64_t ts = time(NULL);

        if (snprintf(filename, sizeof(filename), "%lu.txt", ts) < 0) {
            perror("[ERROR] failed to create filename for output file");
            return 1;
        }

        srv.output = fopen(filename, "w");

        if (srv.output == NULL) {
            perror("[ERROR] failed to open output file");
            return 1;
        }

        srv.output_type = FILE_LOG;
    }

    srv.clients = calloc(sizeof(struct client), srv.max_conn);

    if (srv.clients == NULL) {
        srv_error(&srv, "failed allocation client connection data: %s\n", strerror(errno));

        close_server_output(&srv);

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

    srv_info(&srv, "creating listening socket\n");

    srv.listen_sock = bind_and_listen(&srv, listen_port);
    FD_SET(srv.listen_sock, &srv.all_socks);

    srv.max_socket = srv.listen_sock;

    // ------------------------------------------------------------------------
    // main loop
    // ------------------------------------------------------------------------
    while (1) {
        call_set = srv.all_socks;

        srv_info(&srv, "waiting for activity\n");

        // we are going to use pselect as it will help to handle signal
        // interupts and if we ever pass timeouts to this we will not have to
        // worry about it changing our timeout struct
        int num_s = pselect(srv.max_socket + 1, &call_set, NULL, NULL, NULL, &oldset);

        if (num_s < 0) {
            if (errno == EINTR) {
                srv_info(&srv, "signal interupt\n");
                break;
            } else {
                srv_error(&srv, "pselect: %s\n", strerror(errno));
                break;
            }
        }

        for (int s = 3; s <= srv.max_socket; ++s){
            if (!FD_ISSET(s, &call_set)) {
                continue;
            }

            if (s == srv.listen_sock) {
                server_accept(&srv);
            } else {
                struct client *curr = NULL;

                // if we do not find the client from the list of known clients
                // then there is a logic bug somewhere
                for (size_t index = 0; index < srv.max_conn; ++index) {
                    if (!srv.clients[index].active) {
                        continue;
                    }

                    if (srv.clients[index].sock == s) {
                        curr = &srv.clients[index];
                        break;
                    }
                }

                if (curr == NULL) {
                    srv_error(&srv, "failed to find client based on socket: %d\n", s);
                } else {
                    handle_client(&srv, curr);
                }
            }
        }
    }

    srv_info(&srv, "closing active sockets\n");

    close(srv.listen_sock);

    for (size_t index = 0; index < srv.max_conn; ++index) {
        if (!srv.clients[index].active) {
            continue;
        }

        close(srv.clients[index].sock);

        clear_client(&srv.clients[index]);
    }

    free(srv.clients);

    close_server_output(&srv);

    return 0;
}

void srv_log(struct server* server, const char* format, ...) {
    va_list ap;

    va_start(ap, format);

    vfprintf(server->output, format, ap);

    va_end(ap);

    if (server->output_type == FILE_LOG) {
        fflush(server->output);
    }
}

void srv_info(struct server* server, const char* format, ...) {
    va_list ap;

    va_start(ap, format);

    fprintf(server->output, "[INFO] ");
    vfprintf(server->output, format, ap);

    va_end(ap);

    if (server->output_type == FILE_LOG) {
        fflush(server->output);
    }
}

void srv_warn(struct server* server, const char* format, ...) {
    va_list ap;

    va_start(ap, format);

    fprintf(server->output, "[WARN] ");
    vfprintf(server->output, format, ap);

    va_end(ap);

    if (server->output_type == FILE_LOG) {
        fflush(server->output);
    }
}

void srv_error(struct server* server, const char* format, ...) {
    va_list ap;

    va_start(ap, format);

    fprintf(server->output, "[ERROR] ");
    vfprintf(server->output, format, ap);

    va_end(ap);

    if (server->output_type == FILE_LOG) {
        fflush(server->output);
    }
}

void srv_debug(struct server* server, const char* format, ...) {
    va_list ap;

    va_start(ap, format);

    fprintf(server->output, "[DEBUG] ");
    vfprintf(server->output, format, ap);

    va_end(ap);

    if (server->output_type == FILE_LOG) {
        fflush(server->output);
    }
}

void handle_signal(int signo) {
    if (!TEST_OUTPUT) {
        // dont do anything special, just log the signal
        printf("[server] received signal %d\n", signo);
    }
}

void clear_client_files(struct client *c) {
    for (size_t index = 0; index < c->files_len; ++index) {
        free(c->files[index]);
    }

    free(c->files);

    // avoid dangling pointers
    c->files = NULL;
}

void clear_client(struct client *c) {
    clear_client_files(c);

    c->active = false;
    c->id = 0;
    c->sock = 0;
    c->files_len = 0;
}

void close_server_output(struct server* s) {
    if (s->output_type != FILE_LOG) {
        return;
    }

    if (fclose(s->output) != 0) {
        perror("[server] failed to close output file");
    }
}

void server_accept(struct server* server) {
    srv_info(server, "accepting new connection\n");

    struct sockaddr client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_sock = accept(server->listen_sock, &client_addr, &client_len);

    if (client_sock == -1) {
        srv_error(server, "failed to accept client: %s\n", strerror(errno));
        return;
    }

    if (server->active_clients == server->max_conn - 1) {
        srv_warn(server, "max server connections reached\n");

        if (close(client_sock) != 0) {
            srv_error(server, "error closing connected socket: %s\n", strerror(errno));
            return;
        }
    }

    {
        // this is more for logging and checking that address are
        // they are supposed to be when sent back in a search
        char ip[IPLEN_AND_PORT];

        if (get_ip_port(&client_addr, ip, IPLEN_AND_PORT, true) == NULL) {
            srv_error(server, "failed to create ip string from client: %s\n", strerror(errno));
        } else {
            srv_info(server, "client addr: %s\n", ip);
        }
    }

    FD_SET(client_sock, &server->all_socks);

    if (client_sock > server->max_socket) {
        server->max_socket = client_sock;
    }

    for (size_t index = 0; index < server->max_conn; ++index) {
        if (!server->clients[index].active) {
            server->clients[index].active = true;
            server->clients[index].sock = client_sock;
            server->clients[index].addr = client_addr;
            server->active_clients += 1;
            break;
        }
    }
}

void handle_client(struct server* server, struct client* client) {
    uint8_t recv_buffer[BUFF_SIZE];
    // the server currently makes the assumption that we will
    // receive all the bytes necessary to handle the request
    ssize_t read = recv(client->sock, recv_buffer, BUFF_SIZE, 0);

    if (read <= 0) {
        if (read == -1) {
            srv_error(server, "client %d error: %s\n", client->sock, strerror(errno));
        }

        srv_info(server, "client: %d closing\n", client->sock);

        close(client->sock);

        FD_CLR(client->sock, &server->all_socks);

        clear_client(client);
        server->active_clients -= 1;

        return;
    }

    srv_debug(server, "client %d data:\n", client->sock);

    print_buffer(server->output, recv_buffer, (size_t)read, VERBOSE);

    switch (recv_buffer[0]) {
    case 0: // JOIN
        handle_join(server, client, recv_buffer + 1, (size_t)read - 1);
        break;
    case 1: // PUBLISH
        handle_publish(server, client, recv_buffer + 1, (size_t)read - 1);
        break;
    case 2: // SEARCH
        handle_search(server, client, recv_buffer + 1, (size_t)read - 1);
        break;
    default:
        srv_warn(server, "unknown command received from client: %u\n", recv_buffer[0]);
        break;
    }
}

void handle_join(struct server *server, struct client *client, uint8_t *buffer, size_t len) {
    if (len != 4) {
        srv_warn(server, "handle_join: bytes received is not 4\n");
        return;
    }

    uint32_t received_id = 0;

    memcpy(&received_id, buffer, 4);
    received_id = ntohl(received_id);

    srv_info(server, "handle_join: client joining registry. id: %u\n", received_id);

    for (size_t index = 0; index < server->max_conn; ++index) {
        if (!server->clients[index].active) {
            continue;
        }

        if (server->clients[index].id == received_id) {
            // more for logging purposes
            if (server->clients[index].sock != client->sock) {
                if (client->type == CLIENT_JOINED) {
                    srv_warn(server, "handle_join: client already registered\n");
                } else {
                    srv_warn(server, "handle_join: client has been REGISTERED\n");
                }
            } else {
                srv_warn(server, "handle_join: client id already registered\n");
            }

            break;
        } else {

            char ip[IPLEN_AND_PORT];

            if (get_ip_port(&client->addr, ip, IPLEN_AND_PORT, true) == NULL) {
                srv_error(server, "handle_join: failed to create ip string from client: %s\n", strerror(errno));
                srv_info(server, "handle_join: client registered %u\n", received_id);
            } else {
                srv_info(server, "handle_join: client addr: %s -> %u\n", ip, received_id);
            }

            if (TEST_OUTPUT) {
                printf("TEST] JOIN %u\n", received_id);
            }

            client->id = received_id;
            client->type = CLIENT_JOINED;

            break;
        }
    }
}

void handle_publish(struct server *server, struct client *client, uint8_t *buffer, size_t len) {
    if (client->type == CLIENT_UNKNOWN) {
        srv_warn(server, "handle_publish: client has not joined or registered\n");
        return;
    }

    if (len >= 1199) {
        srv_warn(server, "handle_publish: bytes received is greater than 1200\n");
        return;
    }

    srv_info(server, "handle_publish: client %u publishing files\n", client->id);

    size_t files_len = 0;

    if (len < 4) {
        srv_warn(server, "handle_publish: too few bytes received\n");
        return;
    }

    memcpy(&files_len, buffer, 4);
    files_len = ntohl(files_len);

    if (files_len > server->max_files) {
        srv_warn(server, "handle_publish: number of files is greater than max. given: %lu\n", files_len);
        return;
    }

    // flag for indicating if we need to cleanup due to an error or issue from
    // the client
    bool clean_up = false;
    // in the event that we have to clean up we will use this to keep track of
    // what we have already allocated
    size_t allocated = 0;
    // moving pointer for our current location in the buffer
    uint8_t *p = buffer + 4;
    // pre-allocate the list and we will not re-allocate
    char **files = calloc(sizeof(char *), files_len);

    len -= 4;

    // this is probably the portion that can have the most issue due to
    // allocating strings and having to keep track of what bytes we are
    // looking at
    for (size_t count = 0; count < files_len; ++count) {
        bool found_null = false;
        size_t str_len = 0;

        //srv_debug(server, "handle_publish: len? %lu\n", len);

        for (; str_len < len; ++str_len) {
            if (p[str_len] == 0) {
                found_null = true;
                break;
            } else if (p[str_len] >= 128) {
                srv_warn(server, "handle_publish: invalid ASCII character received from client\n");

                clean_up = true;

                break;
            }
        }

        if (!found_null) {
            srv_warn(server, "handle_publish: non null terminated string given by client\n");

            clean_up = true;

            break;
        }

        char *str = calloc(sizeof(char), str_len);

        if (str == NULL) {
            srv_error(server, "handle_publish: failed allocating string\n");

            clean_up = true;

            break;
        }

        strncpy(str, (char *)p, str_len);

        files[allocated] = str;
        allocated += 1;

        srv_info(server, "handle_publish: str: \"%s\" %lu\n", str, str_len);

        p += str_len + 1;
        len -= str_len + 1;
    }

    if (clean_up) {
        srv_log(server, "handle_publish: cleaning up allocated strings\n");

        for (size_t index = 0; index < allocated; ++index) {
            free(files[index]);
        }

        free(files);
    } else {
        // on the off chance that they have already published files to the 
        // server we will attempt to clean up any previous files
        clear_client_files(client);

        client->files_len = files_len;
        client->files = files;

        srv_info(server, "handle_publish: published files\n");

        for (size_t index = 0; index < client->files_len; ++index) {
            srv_log(server, "    %s\n", client->files[index]);
        }

        if (TEST_OUTPUT) {
            printf("TEST] PUBLISH %lu", client->files_len);

            for (size_t index = 0; index < client->files_len; ++index) {
                printf(" %s", client->files[index]);
            }

            printf("\n");
        }
    }
}

struct client* search_client_files(struct server* server, const char *find, struct client *client) {
    // if we had string lengths before hand this could probably be simpler
    for (size_t file_index = 0; file_index < client->files_len; ++file_index) {
        bool invalid = false;
        bool reached_end = false;
        size_t index = 0;

        srv_debug(server, "     checking \"%s\"\n", client->files[file_index]);

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
        srv_warn(server, "handle_publish: client has not joined or registered\n");
        return;
    }

    if (len >= 100) {
        srv_warn(server, "handle_search: received too many bytes from client\n");
        return;
    }

    uint8_t response[10] = {0};

    // check to make sure that the string we are given is a valid ASCII string
    for (size_t check = 0; check < len; ++check) {
        if (buffer[check] >= 128) {
            srv_warn(server, "handle_search: file name contains non ASCII characters\n");

            if (send_bytes(client->sock, response, 10) != 0) {
                srv_error(server, "handle_search: error sending resposne: %s\n", strerror(errno));
                return;
            }
        }
    }

    if (buffer[len - 1] != 0) {
        srv_warn(server, "handle_search: non null terminated string from client\n");

        if (send_bytes(client->sock, response, 10) != 0) {
            srv_error(server, "handle_search: error sending resposne: %s\n", strerror(errno));
            return;
        }
    }

    struct client *found = NULL;
    char *p = (char *)buffer;

    srv_info(server, "handle_search: client %u searching files for %s\n", client->id, p);

    for (size_t index = 0; index < server->max_conn; ++index) {
        if (!server->clients[index].active) {
            continue;
        }

        srv_debug(server, "handle_search: checking client: %u\n", server->clients[index].id);

        found = search_client_files(server, p, &server->clients[index]);

        if (found != NULL) {
            srv_info(server, "handle_search: found file. id: %u\n", found->id);

            break;
        }
    }

    if (found == NULL) {
        srv_info(server, "handle_search: failed to find file\n");

        if (TEST_OUTPUT) {
            printf("TEST] SEARCH %s 0 0.0.0.0:0\n", p);
        }
    } else {
        // since we do not care if the client connects with an v4 or v6
        // address we have to check to make sure that the client is v4
        if (found->addr.sa_family == AF_INET) {
            uint32_t id = htonl(found->id);
            memcpy(response, &id, 4);

            struct sockaddr_in *v4 = (struct sockaddr_in *)&found->addr;
            memcpy(response + 4, &v4->sin_addr.s_addr, 4);
            memcpy(response + 8, &v4->sin_port, 2);

            if (TEST_OUTPUT) {
                char ip[IPLEN_AND_PORT];

                if (get_ipv4_port(v4, ip, IPLEN_AND_PORT, true) == NULL) {
                    srv_error(server, "handle_search: failed to create ipv4 string from client: %s\n", strerror(errno));
                } else {
                    printf("TEST] SEARCH %s %u %s\n", p, found->id, ip);
                }
            }
        } else {
            srv_warn(server, "handle_search: client is using non IPv4 address\n");
        }
    }

    srv_info(server, "handle_search: sending response\n");

    if (send_bytes(client->sock, response, 10) != 0) {
        srv_error(server, "handle_search: error sending resposne: %s\n", strerror(errno));
    }
}

char* get_ipv4_port(struct sockaddr_in* addr, char* str, size_t len, bool inc_port) {
    if (inet_ntop(AF_INET, &addr->sin_addr, str, len) == NULL) {
        return NULL;
    }

    if (inc_port) {
        // short cut this since the minimum size of an IPv4 address is 7
        // characters
        size_t wrote = strlen(str + 7) + 7;

        if (wrote + 6 > len) {
            return str;
        }

        if (snprintf(str + wrote, len - wrote, ":%u", ntohs(addr->sin_port)) < 0) {
            return NULL;
        } else {
            return str;
        }
    } else {
        return str;
    }
}

char* get_ipv6_port(struct sockaddr_in6* addr, char* str, size_t len, bool inc_port) {
    if (inet_ntop(AF_INET6, &addr->sin6_addr, str, len) == NULL) {
        return NULL;
    }

    if (inc_port) {
        // the address could be between 3 and 45 characters for IPv6 so
        // there is not much of a gain here
        size_t wrote = strlen(str + 3) + 3;

        if (wrote + 6 > len) {
            return str;
        }

        if (snprintf(str + wrote, len - wrote, ":%u", ntohs(addr->sin6_port)) < 0) {
            return NULL;
        } else {
            return str;
        }
    } else {
        return str;
    }
}

char* get_ip_port(struct sockaddr* addr, char* str, size_t len, bool inc_port) {
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)addr;

        return get_ipv4_port(v4, str, len, inc_port);
    } else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)addr;

        return get_ipv6_port(v6, str, len ,inc_port);
    } else {
        return NULL;
    }
}

// pulled from the h1-counter
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

void print_buffer(FILE* output, const uint8_t *buff, size_t length, uint8_t flags) {
    fprintf(output, "buffer:");

    for (size_t index = 0; index < length; ++index) {
        if (buff[index] <= 0x0f) {
            fprintf(output, " 0%x", buff[index]);
        } else {
            fprintf(output, " %x", buff[index]);
        }
    }

    if ((flags & VERBOSE) == VERBOSE) {
        fprintf(output, "\n      :");

        for (size_t index = 0; index < length; ++index) {
            if (buff[index] == '\n') {
                // if the characters is \n then we will escape and display it
                fprintf(output, " \\n");
            } else if (buff[index] < 32) {
                // vs trying to print the control characters we will just print
                // CC for "control character"
                fprintf(output, " CC");
            } else if (buff[index] >= 128) {
                // this is an extended ascii character but we are not going to
                // print it
                fprintf(output, " EE");
            } else {
                // a printable character
                fprintf(output, "  %c", buff[index]);
            }
        }

        fprintf(output, "\n");
    } else {
        fprintf(output, "\n");
    }
}

int bind_and_listen(struct server* server, const char *service) {
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
        srv_error(server, "bind_and_listen: getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    /* Iterate through the address list and try to perform passive open */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if ((s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1) {
            continue;
        }

        if (bind(s, rp->ai_addr, rp->ai_addrlen) == 0) {
            //perror("[server] bind_and_listen: bind");
            break;
        }

        close(s);
    }

    if (rp == NULL) {
        return -1;
    }

    if (listen(s, MAX_PENDING) == -1) {
        srv_error(server, "bind_and_listen: failed to listen on socket: %s\n", strerror(errno));

        close(s);

        return -1;
    }

    freeaddrinfo(result);

    return s;
}
