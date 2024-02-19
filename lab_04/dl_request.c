#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

#define BUF_LEN 2048

/**
 * attempts to send the entire contents of a given buffer
 */
int send_bytes(int sockfd, const char *bytes, size_t length);

/**
 * attempts to connect to the specified remote host.
 */
int connect_socket(const char *host, const char *service);

int main(int argc, char **argv) {
    const char *host = "www.ecst.csuchico.edu";
    const char *port = "80";
    const char *request = "GET /~kkredo/reset_instructions.pdf HTTP/1.0\r\n\r\n";

    FILE *fptr = fopen("./local_file", "w");

    if (fptr == NULL) {
        perror("[dl_request]: main: failed to open file for output");

        return 1;
    }

    int sockfd = connect_socket(host, port);

    if (sockfd < 0) {
        return 1;
    }

    if (send_bytes(sockfd, request, strlen(request)) == -1) {
        perror("[dl_request]: main: failed to send bytes to remote host");

        close(sockfd);

        return 1;
    }

    size_t total_wrote = 0;
    ssize_t total_read = 0;

    uint8_t buffer[BUF_LEN];

    while (1) {
        ssize_t read = recv(sockfd, buffer, BUF_LEN, 0);

        if (read <= 0) {
            if (read == -1) {
                perror("[dl_request]: main error read data from remote host");
            }

            break;
        }

        total_read += read;

        size_t wrote = fwrite(buffer, sizeof(buffer[0]), read, fptr);

        if (wrote == 0) {
            // we wrote 0 bytes... so break?
            break;
        }

        total_wrote += wrote;
    }

    fclose(fptr);
    close(sockfd);

    return 0;
}

int send_bytes(int sockfd, const char *bytes, size_t length) {
    ssize_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(sockfd, bytes + total_sent, length - total_sent, 0);

        if (sent == -1) {
            return -1;
        }

        total_sent += sent;
    }

    return 0;
}

int connect_socket(const char *host, const char *service) {
    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int check = getaddrinfo(host, service, &hints, &result);

    if (check != 0) {
        fprintf(stderr, "[dl_request]: connect_socket: getaddrinfo: %s\n", gai_strerror(check));
        return -1;
    }

    struct addrinfo *rp = result;

    for (; rp != NULL; rp = rp->ai_next) {
        int sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sockfd == -1) {
            perror("[dl_request]: connect_socket: socket: failed to create socket");

            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            freeaddrinfo(result);

            return sockfd;
        }

        perror("[dl_request]: connect_socket: connect: failed connecting to remote address");

        close(sockfd);
    }

    freeaddrinfo(result);

    return -1;
}
