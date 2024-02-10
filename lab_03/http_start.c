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

/*
 * Lookup a host IP address and connect to it using service. Arguments match the first two
 * arguments to getaddrinfo(3).
 *
 * Returns a connected socket descriptor or -1 on error. Caller is responsible for closing
 * the returned socket.
 */
int lookup_and_connect(const char *host, const char *service);

int main( ) {
	int s;
	const char *host = "www.ecst.csuchico.edu";
	const char *port = "80";
    const char *request = "GET /~kkredo/file.html HTTP/1.0\r\n\r\n";
    char buf[BUFSIZ];
    ssize_t total_bytes_received = 0;

	/* Lookup IP and connect to server */
	if ((s = lookup_and_connect(host, port)) < 0 ){
		exit(1);
	}

	/* Modify the program so it
	 *
	 * 1) connects to www.ecst.csuchico.edu on port 80 (mostly done above)
	 * 2) sends "GET /~kkredo/file.html HTTP/1.0\r\n\r\n" to the server
	 * 3) receives all the data sent by the server (HINT: "orderly shutdown" in recv(2))
	 * 4) prints the total number of bytes received
	 *
	 * */

    /* Lookup IP and connect to server */

    /* Send the HTTP request to the server */
    ssize_t bytes_sent = send(s, request, strlen(request), 0);
    if (bytes_sent < 0) {
        perror("send");
        exit(1);
    }

    /* Receive all data sent by the server */
    ssize_t bytes_received;
    while ((bytes_received = recv(s, buf, BUFSIZ, 0)) > 0) {
        total_bytes_received += bytes_received;
    }

    if (bytes_received < 0) {
        perror("recv");
        exit(1);
    }

    /* Print the total number of bytes received */
    printf("Total bytes received: %zd\n", total_bytes_received);

    /* Close the socket */
    close(s);

	return 0;
}

int lookup_and_connect( const char *host, const char *service ) {
	struct addrinfo hints;
	struct addrinfo *rp, *result;
	int s;

	/* Translate host name into peer's IP address */
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	if ((s = getaddrinfo( host, service, &hints, &result )) != 0) {
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
