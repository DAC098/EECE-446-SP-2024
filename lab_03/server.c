#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SERVER_PORT "5432"
#define BUF_SIZE 256
#define MAX_PENDING 5

/**
 * Create, bind and passive open a socket on a local interface for the provided service.
 * Argument matches the second argument to getaddrinfo(3).
 *
 * Returns a passively opened socket or -1 on error. Caller is responsible for calling
 * accept and closing the socket.
 */
int bind_and_listen(const char *service);

/**
 * send a buffer in its entirety to the given socket
 *
 * returns the result of the send operation. will return on first error
 */
int send_bytes(int sockfd, uint8_t *bytes, size_t length);

/**
 * prints the given buffer to stdout
 */
void print_buffer(const uint8_t *buffer, size_t len);

int main(void) {
    char opt;

    int serverfd, clientfd;

    uint32_t lhs, rhs;
    uint64_t opt_result;

    struct sockaddr client_addr;
    socklen_t client_len;

    if ((serverfd = bind_and_listen(SERVER_PORT)) < 0) {
        exit(1);
    }

    uint8_t send_buffer[BUF_SIZE];
    uint8_t recv_buffer[BUF_SIZE];
    ssize_t read;
    ssize_t total_read;
    ssize_t session_read = 0;

    while (1) {
        memset(&client_addr, 0, sizeof(struct sockaddr));

        printf("waiting for client connection\n");

        // wait for a connection to the server. we will only handle one client
        // at a time
        clientfd = accept(serverfd, &client_addr, &client_len);

        if (clientfd == -1) {
            perror("[server] failed to accept client");
            continue;
        }

        printf("accpeted client. checking for data\n");

        while (1) {
            total_read = 0;

            while (total_read < 9) {
                // Receive two uint32_t values into a buffer (buf)
                read = recv(clientfd, recv_buffer, BUF_SIZE, 0);

                if (read <= 0) {
                    if (read == -1) {
                        perror("[server] error reading data from client");
                    }

                    break;
                }

                total_read += read;
            }

            // the client did not send enough data so break out and close the
            // client
            if (total_read < 9) {
                printf("not enough bytes received from client\n");
                break;
            }

            // the client has sent too many bytes so break out and close the
            // client
            if (total_read > 9) {
                printf("too many bytes received from client\n");
                break;
            }

            session_read += total_read;

            printf("received buffer\n");

            print_buffer(recv_buffer, total_read);

            // we will manually fill the buffer by taking the unsigned 32 bit
            // int and placing each byte into the buffer in big-endian order
            lhs = ((uint32_t)(recv_buffer[0] << 24)) |
                ((uint32_t)(recv_buffer[1] << 16)) |
                ((uint32_t)(recv_buffer[2] << 8)) |
                ((uint32_t)(recv_buffer[3]));

            opt = (char)(recv_buffer[4]);

            rhs = ((uint32_t)(recv_buffer[5] << 24)) |
                ((uint32_t)(recv_buffer[6] << 16)) |
                ((uint32_t)(recv_buffer[7] << 8)) |
                ((uint32_t)(recv_buffer[8]));

            // perform the given operation by the client. currently only sum
            switch (opt) {
            case '+':
                opt_result = (uint64_t)lhs + (uint64_t)rhs;
                printf("adding requested numbers %u + %u = %lu\n", lhs, rhs, opt_result);
                break;
            default:
                printf("unknown opt from client\n");
                opt_result = 0;
                break;
            }

            // place the result of the opt into the buffer manually
            send_buffer[0] = (uint8_t)((opt_result & 0xff00000000000000) >> 56);
            send_buffer[1] = (uint8_t)((opt_result & 0x00ff000000000000) >> 48);
            send_buffer[2] = (uint8_t)((opt_result & 0x0000ff0000000000) >> 40);
            send_buffer[3] = (uint8_t)((opt_result & 0x000000ff00000000) >> 32);
            send_buffer[4] = (uint8_t)((opt_result & 0x00000000ff000000) >> 24);
            send_buffer[5] = (uint8_t)((opt_result & 0x0000000000ff0000) >> 16);
            send_buffer[6] = (uint8_t)((opt_result & 0x000000000000ff00) >> 8);
            send_buffer[7] = (uint8_t)( opt_result & 0x00000000000000ff);

            printf("sending buffer\n");
            print_buffer(send_buffer, 8);

            // send back the result of the opt
            if (send_bytes(clientfd, send_buffer, 8) == -1) {
                perror("[server] failed sending data to client");
                break;
            }
        }

        if (shutdown(clientfd, SHUT_RDWR) != 0) {
            perror("[server] failed to shutdown client socket");
        }

        close(clientfd);

        printf("total bytes read from client: %lu\n", session_read);
    }

    close(serverfd);

    return 0;
}

int bind_and_listen(const char *service) {
    struct addrinfo hints;
    struct addrinfo *rp, *result;
    int sockfd;

    // build address data structure
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    // get local address info
    if ((sockfd = getaddrinfo(NULL, service, &hints, &result)) != 0) {
        fprintf(stderr, "[server] bind_and_listen: getaddrinfo: %s\n", gai_strerror(sockfd));
        return -1;
    }

    // iterate through the address list and try to perform passive open
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if ((sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1 ) {
            continue;
        }

        if (!bind(sockfd, rp->ai_addr, rp->ai_addrlen)) {
            break;
        }

        close(sockfd);
    }

    if (rp == NULL) {
        perror("[server] bind_and_listen: bind" );
        return -1;
    }

    if (listen(sockfd, MAX_PENDING) == -1) {
        perror("[server] bind_and_listen: listen");

        close(sockfd);
        return -1;
    }

    freeaddrinfo(result);

    return sockfd;
}

int send_bytes(int sockfd, uint8_t *bytes, size_t length) {
    ssize_t sent;
    uint8_t *p = bytes;

    while (length > 0) {
        sent = send(sockfd, bytes, length, 0);

        if (sent <= 0) {
            return -1;
        }

        *p += sent;
        length -= sent;
    }

    return 0;
}

void print_buffer(const uint8_t *buffer, size_t len) {
    printf("[%lu]:", len);

    for (ssize_t i = 0; i < len; ++i) {
        printf(" %02x", buffer[i]);
    }

    printf("\n");
}
