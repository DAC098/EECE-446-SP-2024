#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>

#define SERVER_PORT "5432"
#define MAX_LINE 256
#define MAX_PENDING 5
#define BUFF_SIZE 2048

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

int main(void) {
    // all_sockets stores all active sockets. Any socket connected to the server should
    // be included in the set. A socket that disconnects should be removed from the set.
    // The server's main socket should always remain in the set.
    fd_set all_sockets;
    FD_ZERO(&all_sockets);
    // call_set is a temporary used for each select call. Sockets will get removed from
    // the set by select to indicate each socket's availability.
    fd_set call_set;
    FD_ZERO(&call_set);

    printf("[server] creating listening socket\n");

    // listen_socket is the fd on which the program can accept() new connections
    int listen_sock = bind_and_listen(SERVER_PORT);
    FD_SET(listen_sock, &all_sockets);

    // max_socket should always contain the socket fd with the largest value, just one
    // for now.
    int max_socket = listen_sock;

    uint8_t recv_buffer[BUFF_SIZE];

    while (1) {
        call_set = all_sockets;

        printf("[server] waiting for activity\n");

        int num_s = select(max_socket + 1, &call_set, NULL, NULL, NULL);

        if (num_s < 0) {
            perror("[server] ERROR in select() call");
            return -1;
        }

        // Check each potential socket.
        // Skip standard IN/OUT/ERROR -> start at 3.
        for (int s = 3; s <= max_socket; ++s){
            // Skip sockets that aren't ready
            if (!FD_ISSET(s, &call_set)) {
                continue;
            }

            // A new connection is ready
            if (s == listen_sock) {
                printf("[server] accepting new connection\n");

                // What should happen with a new connection?
                // You need to call at least one function here
                // and update some variables.
                struct sockaddr client_addr;
                socklen_t client_len;

                int client_sock = accept(listen_sock, &client_addr, &client_len);

                if (client_sock == -1) {
                    perror("[server] failed to accept client");
                    continue;
                }

                FD_SET(client_sock, &all_sockets);

                if (client_sock > max_socket) {
                    max_socket = client_sock;
                }
            } else { // A connected socket is ready
                // Put your code here for connected sockets.
                // Don't forget to handle a closed socket, which will
                // end up here as well.
                ssize_t read = recv(s, recv_buffer, BUFF_SIZE, 0);

                if (read <= 0) {
                    if (read == -1) {
                        fprintf(stderr, "[server] client %d error: %s\n", s, strerror(errno));
                    }

                    printf("[server] client: %d closing\n", s);

                    close(s);

                    FD_CLR(s, &all_sockets);

                    continue;
                }

                printf("[server] client %d:", s);

                for (ssize_t index = 0; index < read; ++index) {
                    if (recv_buffer[index] <= 0x0f) {
                        printf(" 0%x", recv_buffer[index]);
                    } else {
                        printf(" %x", recv_buffer[index]);
                    }
                }

                printf("\n");

                for (ssize_t index = 0; index < read; ++index) {
                    printf("%c", recv_buffer[index]);
                }

                printf("\n");
            }
        }
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
