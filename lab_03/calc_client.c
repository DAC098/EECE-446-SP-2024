/* This code is an updated version of the sample code from "Computer Networks: A Systems
 * Approach," 5th Edition by Larry L. Peterson and Bruce S. Davis. Some code comes from
 * man pages, mostly getaddrinfo(3). */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

#define SERVER_PORT "5432" // This must match on client and server
#define BUF_SIZE 256 // This can be smaller. What size?

/*
 * Lookup a host IP address and connect to it using service. Arguments match the first two
 * arguments to getaddrinfo(3).
 *
 * Returns a connected socket descriptor or -1 on error. Caller is responsible for closing
 * the returned socket.
 */
int lookup_and_connect(const char *host, const char *service);

int send_bytes(int sockfd, char *bytes, size_t length);

int main(int argc, char *argv[]) {
    char *host;
    char send_buf[BUF_SIZE];
    char recv_buf[BUF_SIZE];

    int sockfd;
    int check;

    uint32_t a, b;
    uint32_t answer;

    ssize_t read;
    ssize_t total_read = 0;

    if (argc == 2) {
        host = argv[1];
    } else {
        fprintf(stderr, "usage: %s host\n", argv[0]);
        exit(1);
    }

    /* Lookup IP and connect to server */
    if ((sockfd = lookup_and_connect(host, SERVER_PORT)) < 0) {
        exit(1);
    }

    while(1) {
        memset(send_buf, 0, BUF_SIZE);
        memset(recv_buf, 0, BUF_SIZE);
        total_read = 0;

        // Get two numbers (a and b) from the user
        printf("a: ");
        check = scanf("%d", &a);

        if (check == 0 || check == EOF) {
            if (check == 0) {
                printf("invalid a value provided");
                continue;
            } else {
                perror("error reading input");
                continue;
            }
        }

        printf("b: ");
        check = scanf("%d", &b);

        if (check == 0 || check == EOF) {
            if (check == 0) {
                printf("invalid a value provided");
                continue;
            } else {
                perror("error reading input");
                continue;
            }
        }

        // Copy the numbers into a buffer (buf)
        send_buf[0] = (char)((a & 0xff000000) >> 24);
        send_buf[1] = (char)((a & 0x00ff0000) >> 16);
        send_buf[2] = (char)((a & 0x0000ff00) >> 8);
        send_buf[3] = (char)(a & 0x000000ff);

        send_buf[4] = '+';

        send_buf[5] = (char)((b & 0xff000000) >> 24);
        send_buf[6] = (char)((b & 0x00ff0000) >> 16);
        send_buf[7] = (char)((b & 0x0000ff00) >> 8);
        send_buf[8] = (char)(b & 0x000000ff);

        // Send the buffer to the server using the connected socket. Only send the bytes for a and b!
        if (send_bytes(sockfd, send_buf, 9) == -1) {
            perror("failed to send data to server");
            continue;
        }

        // Receive the sum from the server into a buffer
        while (total_read < 4) {
            read = recv(sockfd, recv_buf, BUF_SIZE, 0);

            if (read <= 0) {
                if (read == -1) {
                    perror("error reading data from remote host");
                    break;
                }
            }

            total_read += read;
        }

        if (total_read != 4) {
            printf("failed to receive the expected amount of bytes\n");
            continue;
        }

        // Copy the sum out of the buffer into a variable (answer)
        answer = ((uint32_t)(recv_buf[0] << 24)) |
            ((uint32_t)(recv_buf[1] << 16)) |
            ((uint32_t)(recv_buf[2] << 8)) |
            ((uint32_t)(recv_buf[3]));

        // Print the sum
        printf("%d\n", answer);
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
        fprintf(stderr, "[calc_client] lookup_and_connect: getaddrinfo: %s\n", gai_strerror(sockfd));
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
        perror("[calc_client] lookup_and_connect: connect");
        return -1;
    }

    freeaddrinfo(result);

    return sockfd;
}

int send_bytes(int sockfd, char *bytes, size_t length) {
    ssize_t sent;
    char *p = bytes;

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
