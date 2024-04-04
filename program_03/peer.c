// Program_02 - Peer-to-Peer Introduction
// Madison Webb and David Cathers

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <netdb.h>

#define MAX_FILES 100
#define FILE_PATH_BUFFER_SIZE 512
#define MAX_FILENAME_LENGTH 100

int sock;
unsigned int peer_id;
char peerIPStrGlobal[INET_ADDRSTRLEN]; // to store the peer's IP address as a string
unsigned short peerPortGlobal;         // to store the peer's port number

void join()
{
    // buffer for the JOIN request
    unsigned char buffer[5];
    buffer[0] = 0; // action code for JOIN is 0
    *(unsigned int *)(buffer + 1) = htonl(peer_id);

    // send the JOIN request to the registry
    if (send(sock, buffer, sizeof(buffer), 0) < 0)
    {
        perror("send failed");
        return;
    }

    printf("JOIN request sent. Peer ID: %u\n", peer_id);
}

void publish()
{
    DIR *dir;
    struct dirent *ent;
    char buffer[1200];
    int offset = 5; // start after action and count bytes
    unsigned int fileCount = 0;
    char filePath[FILE_PATH_BUFFER_SIZE]; // buffer for constructing file paths

    // open the SharedFiles directory
    dir = opendir("./SharedFiles");
    if (dir == NULL)
    {
        perror("Unable to open directory");
        return;
    }
    while ((ent = readdir(dir)) != NULL && offset < sizeof(buffer))
    {
        snprintf(filePath, sizeof(filePath), "./SharedFiles/%s", ent->d_name);
        struct stat statbuf;
        if (stat(filePath, &statbuf) == 0)
        {
            if (S_ISREG(statbuf.st_mode))
            {
                int nameLen = strlen(ent->d_name);
                if (offset + nameLen + 1 < sizeof(buffer))
                {
                    strcpy(buffer + offset, ent->d_name); // copy the file name
                    offset += nameLen + 1;                // move offset, account for null terminator
                    fileCount++;
                }
                else
                {
                    fprintf(stderr, "Buffer full, some files may not be published.\n");
                    break; // buffer full, stop processing
                }
            }
        }
    }

    closedir(dir);

    buffer[0] = 1;                                    // action code for PUBLISH
    *(unsigned int *)(buffer + 1) = htonl(fileCount); // place the file count at the beginning of the buffer, in network byte order

    // send the PUBLISH request to the registry
    if (send(sock, buffer, offset, 0) < 0)
    {
        perror("send");
    }
    else
    {
        printf("Successfully published %u files.\n", fileCount);
    }
}

void search()
{
    char fileName[101]; // buffer to hold file name
    printf("Enter a file name: \n");
    scanf("%100s", fileName); // read file name, ensuring not to overflow buffer
    char buffer[1024];
    int fileNameLength = strlen(fileName);
    buffer[0] = 2; // action code for SEARCH
    strcpy(buffer + 1, fileName);
    buffer[fileNameLength + 1] = '\0';

    // send the SEARCH request to the registry
    if (send(sock, buffer, fileNameLength + 2, 0) < 0)
    {
        perror("send");
        return;
    }

    // receive the response from the registry
    unsigned int peerID;
    unsigned int peerIPv4;
    unsigned short peerPort;

    int bytesReceived = recv(sock, &peerID, sizeof(peerID), 0);
    if (bytesReceived <= 0)
    {
        perror("recv");
        return;
    }

    bytesReceived += recv(sock, &peerIPv4, sizeof(peerIPv4), 0);
    bytesReceived += recv(sock, &peerPort, sizeof(peerPort), 0);

    if (bytesReceived < sizeof(peerID) + sizeof(peerIPv4) + sizeof(peerPort))
    {
        fprintf(stderr, "Incomplete response from registry.\n");
        return;
    }

    // convert network byte order to host byte order
    peerID = ntohl(peerID);
    peerPort = ntohs(peerPort);

    // check if file was found
    if (peerID == 0 && peerIPv4 == 0 && peerPort == 0)
    {
        printf("File not indexed by registry.\n");
    }
    else
    {
        // convert peer IPv4 address to human-readable form
        char peerIPStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peerIPv4, peerIPStr, INET_ADDRSTRLEN);

        printf("File found at\nPeer %u\n%s:%hu\n", peerID, peerIPStr, peerPort);
    }
}

void searchForFetch(char *fileName)
{
    char buffer[1024];
    int fileNameLength = strlen(fileName);
    buffer[0] = 2; // action code for SEARCH
    strcpy(buffer + 1, fileName);
    buffer[fileNameLength + 1] = '\0'; // ensure null-termination

    // send the SEARCH request to the registry
    if (send(sock, buffer, fileNameLength + 2, 0) < 0)
    {
        perror("send");
        return;
    }

    // expecting to receive peer information: peerID, peerIPv4, peerPort
    unsigned int peerID;
    unsigned int peerIPv4;
    unsigned short peerPort;

    int bytesReceived = recv(sock, &peerID, sizeof(peerID), 0);
    if (bytesReceived <= 0)
    {
        perror("recv");
        return;
    }

    bytesReceived += recv(sock, &peerIPv4, sizeof(peerIPv4), 0);
    bytesReceived += recv(sock, &peerPort, sizeof(peerPort), 0);

    if (bytesReceived < sizeof(peerID) + sizeof(peerIPv4) + sizeof(peerPort))
    {
        fprintf(stderr, "Incomplete response from registry.\n");
        return;
    }

    peerID = ntohl(peerID);
    peerPort = ntohs(peerPort);

    // store the peer's address and port for fetching the file
    if (peerID != 0 && peerIPv4 != 0 && peerPort != 0)
    {
        inet_ntop(AF_INET, &peerIPv4, peerIPStrGlobal, INET_ADDRSTRLEN);
        peerPortGlobal = peerPort;
    }
    else
    {
        printf("File not found in the registry.\n");
    }
}

void fetch()
{
    char fileName[MAX_FILENAME_LENGTH + 1]; // buffer to hold file name
    printf("Enter a file name to fetch: \n");
    scanf("%100s", fileName); // read file name, ensuring not to overflow buffet

    // send a SEARCH request for the file
    searchForFetch(fileName); // Tmodified search function tailored for fetch

    int peerSock = socket(AF_INET, SOCK_STREAM, 0);
    if (peerSock < 0)
    {
        perror("Failed to create socket for fetching file");
        return;
    }

    // setup peer address
    struct sockaddr_in peerAddr;
    memset(&peerAddr, 0, sizeof(peerAddr));
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_port = htons(peerPortGlobal);
    inet_pton(AF_INET, peerIPStrGlobal, &peerAddr.sin_addr);

    // connect to the peer
    if (connect(peerSock, (struct sockaddr *)&peerAddr, sizeof(peerAddr)) < 0)
    {
        perror("Failed to connect to peer for fetching file");
        close(peerSock);
        return;
    }

    // send FETCH request
    char fetchBuffer[101]; // 1 byte for action + 100 for filename
    fetchBuffer[0] = 3;    // action code for FETCH
    strcpy(fetchBuffer + 1, fileName);
    if (send(peerSock, fetchBuffer, strlen(fileName) + 2, 0) < 0)
    { // +2 for action and null terminator
        perror("Failed to send FETCH request");
        close(peerSock);
        return;
    }

    // receive file content
    FILE *file = fopen(fileName, "wb"); // open file for writing in binary mode
    if (file == NULL)
    {
        perror("Failed to open file for writing");
        close(peerSock);
        return;
    }

    int bytesReceived;
    char fileBuffer[4096]; // buffer for file data
    while ((bytesReceived = recv(peerSock, fileBuffer, sizeof(fileBuffer), 0)) > 0)
    {
        fwrite(fileBuffer, sizeof(char), bytesReceived, file);
    }

    if (bytesReceived < 0)
    {
        perror("Failed to receive file data");
    }
    else
    {
        printf("File '%s' fetched successfully.\n", fileName);
    }

    fclose(file);
    close(peerSock);
}

void close_app()
{
    if (sock != -1)
    {
        close(sock); // close the network socket
    }
    printf("Exiting peer application.\n");
    exit(0); // exit the program
}

void print_options()
{
    char selection[10];
    printf("\nAvailable Commands: \n");
    printf("JOIN: sends a JOIN request to the registry.\n");
    printf("PUBLISH: send a PUBLISH request to the registry.\n");
    printf("SEARCH: reads a file name from the terminal, print peer info.\n");
    printf("FETCH: fetch a file from another peer and save it locally.\n");
    printf("EXIT: close the peer application.\n\n");

    while (1)
    {
        printf("Enter a command: \n");
        if (scanf("%9s", selection) == 1)
        {
            if (strcmp(selection, "JOIN") == 0)
            {
                join();
            }
            else if (strcmp(selection, "PUBLISH") == 0)
            {
                publish();
            }
            else if (strcmp(selection, "SEARCH") == 0)
            {
                search();
            }
            else if (strcmp(selection, "FETCH") == 0)
            {
                fetch();
            }
            else if (strcmp(selection, "EXIT") == 0)
            {
                printf("Exiting peer application.\n");
                break; // exit the loop
            }
            else
            {
                printf("Unknown command. Please try again.\n");
            }
        }
        else
        {
            fprintf(stderr, "Error reading input. Please try again.\n");
            while (getchar() != '\n')
                ;
        }
    }
}

int main(int argc, char *argv[])
{
    // ensure correct argument count
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <registry IP> <registry port> <peer ID>\n", argv[0]);
        exit(1);
    }

    peer_id = atoi(argv[3]); // convert peer ID from argument

    // networking initialization
    struct sockaddr_in registryAddr; // registry address
    struct hostent *registryHost;    // host information

    // create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Failed to create socket");
        exit(1);
    }

    // resolve registry hostname/IP
    registryHost = gethostbyname(argv[1]);
    if (registryHost == NULL)
    {
        fprintf(stderr, "ERROR: No such host\n");
        exit(1);
    }

    // fill registry address structure
    memset(&registryAddr, 0, sizeof(registryAddr));
    registryAddr.sin_family = AF_INET;
    memcpy(&registryAddr.sin_addr, registryHost->h_addr_list[0], registryHost->h_length);
    registryAddr.sin_port = htons(atoi(argv[2])); // registry port

    // connect to registry
    if (connect(sock, (struct sockaddr *)&registryAddr, sizeof(registryAddr)) < 0)
    {
        perror("Failed to connect to registry");
        exit(1);
    }
    printf("Connected to registry at %s:%s\n", argv[1], argv[2]);
    print_options();
    close(sock);
    return 0;
}