#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 256

/**
 * Lookup a host IP address and connect to it using service. Arguments match the first two
 * arguments to getaddrinfo(3).
 *
 * Returns a connected socket descriptor or -1 on error. Caller is responsible for closing
 * the returned socket.
 */
int lookup_and_connect(const char *host, const char *service);

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

/**
 * clears the contents of stdin
 */
void clear_stdin();

int main(int argc, char *argv[]) {
    char *host = "::";
    char *port = "5432";

    uint8_t send_buffer[BUF_SIZE];
    uint8_t recv_buffer[BUF_SIZE];

    int sockfd;
    int check;

    uint32_t lhs, rhs;
    uint32_t server_result;

    ssize_t read;
    ssize_t total_read = 0;

    if (argc == 2) {
        host = argv[1];
    }

    if ((sockfd = lookup_and_connect(host, port)) < 0) {
        exit(1);
    }

    while(1) {
        memset(send_buffer, 0, BUF_SIZE);
        memset(recv_buffer, 0, BUF_SIZE);
        total_read = 0;

        // request lhs value from user
        printf("lhs: ");
        check = scanf("%u", &lhs);

        if (check == 0 || check == EOF) {
            clear_stdin();

            if (check == 0) {
                printf("invalid lhs value provided\n");
                continue;
            } else {
                perror("[client] error reading input");
                continue;
            }
        }

        // request rhs value from user
        printf("rhs: ");
        check = scanf("%u", &rhs);

        if (check == 0 || check == EOF) {
            clear_stdin();

            if (check == 0) {
                printf("invalid rhs value provided\n");
                continue;
            } else {
                perror("[client] error reading input");
                continue;
            }
        }

        // we will manually fill the buffer by taking the unsigned 32 bit int
        // and placing each byte into the buffer in big-endian order
        send_buffer[0] = (uint8_t)((lhs & 0xff000000) >> 24);
        send_buffer[1] = (uint8_t)((lhs & 0x00ff0000) >> 16);
        send_buffer[2] = (uint8_t)((lhs & 0x0000ff00) >>  8);
        send_buffer[3] = (uint8_t)( lhs & 0x000000ff);

        send_buffer[4] = (uint8_t)'+';

        send_buffer[5] = (uint8_t)((rhs & 0xff000000) >> 24);
        send_buffer[6] = (uint8_t)((rhs & 0x00ff0000) >> 16);
        send_buffer[7] = (uint8_t)((rhs & 0x0000ff00) >> 8);
        send_buffer[8] = (uint8_t)( rhs & 0x000000ff);

        printf("sending buffer\n");
        print_buffer(send_buffer, 9);

        // send the contents of the buffer to the server. since we only wrote 9
        // bytes to the buffer we will inform the send operation that the
        // buffer is that long even though the buffer size could be larger
        if (send_bytes(sockfd, send_buffer, 9) == -1) {
            perror("[client] failed to send data to server");
            continue;
        }

        // wait for data from server, if the amount is less than 4 then we will
        // wait to read more
        while (total_read < 4) {
            read = recv(sockfd, recv_buffer, BUF_SIZE, 0);

            if (read <= 0) {
                if (read == -1) {
                    perror("[client] error reading data from remote host");
                    break;
                }
            }

            total_read += read;
        }

        // since we are only expecting 4 bytes from the server we will expect
        // only that amount
        if (total_read != 4) {
            fprintf(stderr, "[client] failed to receive the expected amount of bytes\n");
            continue;
        }

        printf("received buffer\n");
        print_buffer(recv_buffer, 4);

        // Copy the sum out of the buffer into a variable (server_result)
        server_result = ((uint32_t)(recv_buffer[0] << 24)) |
            ((uint32_t)(recv_buffer[1] << 16)) |
            ((uint32_t)(recv_buffer[2] << 8)) |
            ((uint32_t)(recv_buffer[3]));

        // Print the sum
        printf("%u + %u = %u\n", lhs, rhs, server_result);
    }

    close(sockfd);

    return 0;
}

int lookup_and_connect(const char *host, const char *service) {
    struct addrinfo hints;
    struct addrinfo *rp, *result;
    int sockfd;

    /* Translate host name into peer's IP address */
    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    if ((sockfd = getaddrinfo(host, service, &hints, &result)) != 0) {
        fprintf(stderr, "[client] lookup_and_connect: getaddrinfo: %s\n", gai_strerror(sockfd));
        return -1;
    }

    /* Iterate through the address list and try to connect */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if ((sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1) {
            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }

        close(sockfd);
    }

    if (rp == NULL) {
        perror("[client] lookup_and_connect: connect");
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
    printf("[%ld]:", len);

    for (ssize_t i = 0; i < len; ++i) {
        printf(" %02x", buffer[i]);
    }

    printf("\n");
}

void clear_stdin() {
    int c;

    while((c = getchar()) != '\n' && c != EOF) {}
}
