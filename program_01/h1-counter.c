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

/**
 * attempts to send the entire contents of a given buffer
 */
int send_bytes(int sockfd, const char *bytes, size_t length);

/**
 * prints the contents of the given addrinfo struct
 */
void print_addrinfo(struct addrinfo *rp, int include_port);

/**
 * prints the contents of a given buffer
 */
void print_buffer(const char *buffer, size_t len);

/**
 * binds the client socket to a specified interface and port
 */
int bind_socket(int *s, int af_family, const char *l_ip, int verbose);

/**
 * attempts to connect to the specified remote host.
 * can also provide a local interface and port if a specific interface is
 * required.
 */
int connect_socket(
    int *sockfd,
    const char *l_ip,
    const char *r_host,
    const char *r_port,
    int verbose
);

/**
 * parses a given string as an unsigned long
 */
int parse_ul(char *str, unsigned long *value);

/**
 * finds the number of occurances for needle in haystack
 */
size_t count_occurances(
    const char* haystack,
    size_t haystack_len,
    const char* needle,
    size_t needle_len
);

int main(int argc, char **argv) {
    int verbose = 0;
    int fill_buffer = 0;

    size_t buffer_size = 2048;

    // default remote_port for http
    char *remote_port = "80";
    // default local_ip is unspecified
    char *local_ip = NULL;
    // default remote_host
    char *remote_host = "www.ecst.csuchico.edu";

    // named options that the program will accept. each argument requires a
    // value following it. no short hand names
    static struct option long_options[] = {
        {"local-ip", required_argument, 0, 0},
        {"remote-host", required_argument, 0, 0},
        {"remote-port", required_argument, 0, 0},
        {"buffer-size", required_argument, 0 ,0},
        {"verbose", no_argument, 0, 0},
        {"fill-buffer", no_argument, 0, 0},
        {0, 0, 0, 0}
    };

    int c;
    int option_index = 0;

    while (1) {
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
                remote_host = optarg;
                break;
            case 2:
                remote_port = optarg;
                break;
            case 3:
                if (parse_ul(optarg, &buffer_size) != 0) {
                    fprintf(stderr, "invalid buffer-size value: \"%s\"\n", optarg);
                    return 1;
                }

                break;
            case 4:
                verbose = 1;
                break;
            case 5:
                fill_buffer = 1;
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

        if (parse_ul(argv[start], &buffer_size) != 0) {
            fprintf(stderr, "invalid buffer-size value: \"%s\"\n", argv[start]);
            return 1;
        }
    }

    if (remote_host == NULL) {
        fprintf(stderr, "remote-host was not specified\n");
        return 1;
    }

    int sockfd = 0;

    if (connect_socket(&sockfd, local_ip, remote_host, remote_port, verbose) == -1) {
        return 1;
    }

    // since http 1.0 accepts text based requests we don't have to do anything
    // else
    char *request = "GET /~kkredo/file.html HTTP/1.0\r\n\r\n";

    if (verbose) {
        printf("sending request to remote server\n");
    }

    // send the request to the remote server
    if (send_bytes(sockfd, request, strlen(request)) == -1) {
        perror("[h1-counter]: main: failed sending bytes to remote host");

        close(sockfd);

        return 1;
    }

    // values for tracking html tags and string reads
    char *needle = "<h1>";
    size_t needle_len = strlen(needle);
    size_t needle_count = 0;

    // value for tracking ingress
    size_t fill_read = 0;
    size_t total_read = 0;

    // allocate buffer from user specified length
    size_t buffer_len = buffer_size * sizeof(char);
    char *buffer = (char*)malloc(buffer_len);

    while (1) {
        if (fill_buffer) {
            fill_read = 0;

            // here we will read until the buffer is filled completely, until
            // there was an error, or the connection is closed by the remote
            // host
            while (fill_read != buffer_len) {
                ssize_t read = recv(sockfd, buffer + fill_read, buffer_len - fill_read, 0);

                if (read <= 0) {
                    if (read == -1) {
                        perror("[h1-counter]: main: error reading data from remote host");
                    }

                    goto end_main_loop;
                }

                if (verbose) {
                    printf("read %ld bytes from remote server\n", read);
                }

                fill_read += (size_t)read;
                total_read += (size_t)read;
            }

            if (verbose) {
                printf("filled buffer with %lu bytes\n", fill_read);
            }
        } else {
            // we will only read once and what ever amount of data we get is
            // what we will operate on
            ssize_t read = recv(sockfd, buffer, buffer_len, 0);

            if (read <= 0) {
                if (read == -1) {
                    perror("[h1-counter]: main: error reading data from remote host");
                }

                goto end_main_loop;
            }

            if (verbose) {
                printf("read %ld bytes from remote server\n", read);
            }

            total_read += (size_t)read;
            fill_read = (size_t)read;
        }

        if (verbose) {
            printf("checking for needle\n");
        }

        needle_count += count_occurances(buffer, fill_read, needle, needle_len);
    }

// the logic could be split up better so that we dont have to use goto labels
// but for this current situation this will work out just fine since we dont
// have to worry about anything in the main loop
end_main_loop:

    if (fill_buffer && fill_read > 0) {
        if (verbose) {
            printf("%lu unread bytes in buffer. checking for needle\n", fill_read);
        }

        needle_count += count_occurances(buffer, fill_read, needle, needle_len);
    }

    printf("Number of <h1> tags: %lu\nNumber of bytes: %lu\n", needle_count, total_read);

    free(buffer);
    close(sockfd);

    return 0;
}

size_t count_occurances(
    const char* haystack,
    size_t haystack_len,
    const char* needle,
    size_t needle_len
) {
    size_t count = 0;

    if (needle_len == 0) {
        return count;
    }

    for (size_t index = 0; index < haystack_len; ++index) {
        if (haystack[index] == needle[0]) {
            size_t check_read = 0;

            for (; check_read < needle_len && index < haystack_len; ++check_read) {
                if (needle[check_read] != haystack[index]) {
                    break;
                }

                index += 1;
            }

            if (check_read == needle_len) {
                count += 1;
            }
        }
    }

    return count;
}

int parse_ul(char *str, unsigned long *value) {
    char *endptr;
    *value = strtoul(str, &endptr, 0);

    if (*endptr != '\0') {
        return -1;
    }

    if ((*value == ULONG_MAX && errno == ERANGE) || errno == EINVAL) {
        return -1;
    }

    return 0;
}

int send_bytes(int sockfd, const char *bytes, size_t length) {
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(sockfd, bytes + total_sent, length - total_sent, 0);

        if (sent < 0) {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

void print_buffer(const char *buffer, size_t len) {
    printf("[%ld]:", len);

    for (size_t i = 0; i < len; ++i) {
        printf(" %#x", buffer[i]);
    }

    printf("\n%s\n", buffer);
}

void print_addrinfo(struct addrinfo *rp, int include_port) {
    // ipv4 address string will fill inside an ipv6 address string
    char addr_str[INET6_ADDRSTRLEN];

    if (rp->ai_family == AF_INET) {
        // cast the ai_addr to the sockaddr_in struct
        struct sockaddr_in *v4 = (struct sockaddr_in *)rp->ai_addr;

        // attempt to retrieve the string version of the ipv4 address
        if (inet_ntop(rp->ai_family, &(v4->sin_addr), addr_str, INET6_ADDRSTRLEN) == NULL) {
            perror("[h1-counter]: print_addrinfo: inet_ntop: failed parsing addr");
            return;
        }

        if (include_port != 0) {
            printf("%s:%d", addr_str, v4->sin_port);
        } else {
            printf("%s", addr_str);
        }
    } else if (rp->ai_family == AF_INET6) {
        // cast the ai_addr to the sockaddr_in6 struct
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)rp->ai_addr;

        // attempt to retrieve the string version of the ipv6 address
        if (inet_ntop(rp->ai_family, &(v6->sin6_addr), addr_str, INET6_ADDRSTRLEN) == NULL) {
            perror("[h1-counter]: print_addrinfo: inet_ntop: failed parsing addr");
            return;
        }

        if (include_port != 0) {
            printf("%s:%d", addr_str, v6->sin6_port);
        } else {
            printf("%s", addr_str);
        }
    } else {
        // the ai_family is not something that is recognized by the function
        // so just print some small stuff
        printf("family: %d | socktype: %d | protocol: %d", rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    }
}

int bind_socket(int *sockfd, int af_family, const char *l_ip, int verbose) {
    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = af_family;
    hints.ai_socktype = SOCK_STREAM;
    // some of these flags may not be what I think, just tring them
    hints.ai_flags = AI_NUMERICHOST | AI_V4MAPPED;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int s = getaddrinfo(l_ip, NULL, &hints, &result);

    if (s != 0) {
        fprintf(stderr, "[h1-counter]: bind_socket: getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    struct addrinfo *rp = result;

    for (; rp != NULL; rp = rp->ai_next) {
        if (verbose) {
            printf("binding socket ");

            print_addrinfo(rp, 1);

            printf("\n");
        }

        *sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (*sockfd == -1) {
            perror("[h1-counter]: bind_socket: socket: failed to create socket");
            continue;
        }

        if (bind(*sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            freeaddrinfo(result);

            return 0;
        }

        perror("[h1-counter]: bind_socket: bind: failed binding port");
    }

    freeaddrinfo(result);

    return -1;
}

int connect_socket(
    int *sockfd,
    const char *l_ip,
    const char *r_host,
    const char *r_port,
    int verbose
) {
    if (verbose) {
        printf("attempting to connect with remote server %s", r_host);

        if (r_port != NULL) {
            printf(":%s", r_port);
        }

        printf("\n");
    }

    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int s = getaddrinfo(r_host, r_port, &hints, &result);

    if (s != 0) {
        fprintf(stderr, "[h1-counter]: connect_socket: getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    struct addrinfo *rp = result;

    for (; rp != NULL; rp = rp->ai_next) {
        if (l_ip != NULL) {
            if (bind_socket(sockfd, rp->ai_family, l_ip, verbose) == -1) {
                continue;
            }
        } else {
            *sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

            if (*sockfd == -1) {
                perror("[h1-counter]: connect_socket: socket: failed to create socket");
                continue;
            }
        }

        if (verbose) {
            printf("connecting ");

            print_addrinfo(rp, 0);

            printf("\n");
        }

        if (connect(*sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            freeaddrinfo(result);

            return 0;
        }

        perror("[h1-counter]: connect_socket: connect: failed connecting to remote address");

        close(*sockfd);
    }

    freeaddrinfo(result);

    return -1;
}

