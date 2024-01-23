/* This code is an updated version of the sample code from "Computer Networks: A Systems
 * Approach," 5th Edition by Larry L. Peterson and Bruce S. Davis. Some code comes from
 * man pages, mostly getaddrinfo(3). */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#define SERVER_PORT "9000"
#define MAX_LINE 256

#define HTTP_CRLF "\n\r"

/**
 * Lookup a host IP address and connect to it using service. Arguments match the first two
 * arguments to getaddrinfo(3).
 *
 * Returns a connected socket descriptor or -1 on error. Caller is responsible for closing
 * the returned socket.
 */
int lookup_and_connect(const char *host, const char *service);

int main(int argc, char **argv) {
    // will be either a valid ipv4/ipv6 address or dns record to lookup
    unsigned short local_port = 0;
    unsigned short remote_port = 9000;
    char *local_ip = "::\0";
    char *remote_host = NULL;

    static struct option long_options[] = {
        {"local-ip", required_argument, 0, 0},
        {"local-port", required_argument, 0, 0},
        {"remote-host", required_argument, 0, 0},
        {"remote-port", required_argument, 0, 0},
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
                printf("local-ip arg given %s\n", optarg);
                break;
            case 1:
                printf("local-port arg given %s\n", optarg);
                break;
            case 2:
                printf("remote-host arg given %s\n", optarg);
                remote_host = optarg;
                break;
            case 3:
                printf("remote-port arg given %s\n", optarg);
                break;
            default:
                printf("unknown argument given?");
                break;
            }

            break;
        default:
            return 1;
        }
    }

    if (remote_addr == NULL) {
        printf("remote-host was not specified\n");
        return 1;
    }

    return 0;
}

int main2(int argc, char *argv[]) {
    char *host;
    char buf[MAX_LINE];
    int s;
    int len;

    if (argc == 2) {
        host = argv[1];
    } else {
        fprintf(stderr, "usage: %s host\n", argv[0]);
        exit(1);
    }

    /* Lookup IP and connect to server */
    if ((s = lookup_and_connect(host, SERVER_PORT)) < 0) {
        exit(1);
    }

    /* Main loop: get and send lines of text */
    while (fgets(buf, sizeof(buf), stdin)) {
        buf[MAX_LINE - 1] = '\0';
        len = strlen(buf) + 1;

        if (send(s, buf, len, 0) == -1) {
            perror("stream-talk-client: send");
            close(s);

            exit(1);
        }
    }

    close(s);

    return 0;
}

int lookup_and_connect(const char *host, const char *service) {
    struct addrinfo hints;
    struct addrinfo *rp, *result;
    int s;

    /* Translate host name into peer's IP address */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    if ((s = getaddrinfo(host, service, &hints, &result)) != 0) {
        fprintf(stderr, "stream-talk-client: getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    /* Iterate through the address list and try to connect */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if ((s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1) {
            continue;
        }

        if (connect(s, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }

        close(s);
    }

    if (rp == NULL) {
        perror("stream-talk-client: connect");
        return -1;
    }

    freeaddrinfo(result);

    return s;
}
