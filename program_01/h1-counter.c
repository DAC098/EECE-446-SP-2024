#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>

int send_bytes(int sockfd, char *bytes, size_t length);

void print_addrinfo(struct addrinfo *rp);

int bind_socket(int *s, int af_family, const char *l_ip, const char *l_port);

int connect_socket(int *sockfd, const char *l_ip, const char *l_port, const char *r_host, const char *r_port);

int main(int argc, char **argv) {
    char *local_port = "0";
    char *remote_port = "9000";
    char *local_ip = NULL;
    char *remote_host = NULL;
    size_t buffer_size = 2048;

    static struct option long_options[] = {
        {"local-ip", required_argument, 0, 0},
        {"local-port", required_argument, 0, 0},
        {"remote-host", required_argument, 0, 0},
        {"remote-port", required_argument, 0, 0},
        {"buffer-size", required_argument, 0 ,0},
        {0, 0, 0, 0}
    };

    int c;

    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
        case 0:
            switch (option_index) {
            case 0:
                local_ip = optarg;
                break;
            case 1:
                local_port = optarg;
                break;
            case 2:
                remote_host = optarg;
                break;
            case 3:
                remote_port = optarg;
                break;
            case 4:
                size_t check = strtoul(optarg, NULL, 0);

                if (check == ULONG_MAX && errno == ERANGE) {
                    fprintf(stderr, "invalid buffer-size value\n");
                    return 1;
                }

                buffer_size = check;
            default:
                printf("unknown argument given?");
                break;
            }

            break;
        default:
            return 1;
        }
    }

    if (remote_host == NULL) {
        fprintf(stderr, "remote-host was not specified\n");
        return 1;
    }

    int sockfd;

    if (connect_socket(&sockfd, local_ip, local_port, remote_host, remote_port) == -1) {
        return 1;
    }

    char request[] = "GET / HTTP/1.0\r\n";
    size_t length = strlen(request);

    if (send_bytes(sockfd, request, length) == -1) {
        perror("failed sending bytes to remote host");

        close(sockfd);

        return 1;
    }

    char *buffer = (char*)malloc(buffer_size * sizeof(char));
    ssize_t read;
    ssize_t total_read = 0;

    for (uint32_t safety = 0; safety < 1000; ++safety) {
        printf("waiting for data\n");

        read = recv(sockfd, buffer, buffer_size, 0);

        if (read == -1) {
            perror("error reading data from remote host");
            break;
        }

        total_read += read;

        printf("[%ld]:", read);

        for (ssize_t i = 0; i < read; ++i) {
            printf(" %#x", buffer[i]);
        }

        printf("\n");

        if (total_read == length) {
            break;
        }
    }

    free(buffer);
    close(sockfd);

    return 0;
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

void print_addrinfo(struct addrinfo *rp) {
    char addr_str[INET6_ADDRSTRLEN];

    if (rp->ai_family == AF_INET) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)rp->ai_addr;

        if (inet_ntop(rp->ai_family, &(v4->sin_addr), addr_str, INET6_ADDRSTRLEN) == NULL) {
            perror("inet_ntop: failed parsing addr");
            return;
        }

        printf("%s", addr_str);
    } else if (rp->ai_family == AF_INET6) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)rp->ai_addr;

        if (inet_ntop(rp->ai_family, &(v6->sin6_addr), addr_str, INET6_ADDRSTRLEN) == NULL) {
            perror("inet_ntop: failed parsing addr");
            return;
        }

        printf("%s", addr_str);
    } else {
        printf("family: %d | socktype: %d | protocol: %d", rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    }
}

int bind_socket(int *sockfd, int af_family, const char *l_ip, const char *l_port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = af_family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE | AI_V4MAPPED;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int s;

    s = getaddrinfo(l_ip, l_port, &hints, &result);

    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        printf("binding socket ");

        print_addrinfo(rp);

        printf("\n");

        if ((*sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1) {
            perror("-- failed to create socket");
            continue;
        }

        if (bind(*sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            freeaddrinfo(result);

            return 0;
        }

        perror("-- failed binding port");
    }

    freeaddrinfo(result);

    return -1;
}

int connect_socket(int *sockfd, const char *l_ip, const char *l_port, const char *r_host, const char *r_port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int s;

    s = getaddrinfo(r_host, r_port, &hints, &result);

    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        printf("connecting ");

        print_addrinfo(rp);

        printf("\n");

        if (bind_socket(sockfd, rp->ai_family, l_ip, l_port) == -1) {
            continue;
        }

        if (connect(*sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            freeaddrinfo(result);

            return 0;
        }

        perror("-- failed connecting to remote address");

        close(*sockfd);
    }

    freeaddrinfo(result);

    return -1;
}

