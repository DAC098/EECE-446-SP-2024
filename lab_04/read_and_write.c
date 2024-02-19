#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
// check if the correct number of arguments are provided
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

// open the input file for reading
    int input_fd = fopen(argv[1], O_RDONLY);
    if (input_fd == -1) {
        perror("Error opening input file");
        exit(EXIT_FAILURE);
    }

// open the output file for writing
    int output_fd = fopen("upper_file", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (output_fd == -1) {
        perror("Error opening output file");
        exit(EXIT_FAILURE);
    }

// buffer for reading from input file
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

// read from input file, convert to upper case, and write to output file
    while ((bytes_read = read(input_fd, buffer, BUFFER_SIZE)) > 0) {
        // convert characters to upper case
        for (int i = 0; i < bytes_read; i++) {
            buffer[i] = toupper(buffer[i]);
        }

        // write converted data to output file
        if (write(output_fd, buffer, bytes_read) != bytes_read) {
            perror("Error writing to output file");
            exit(EXIT_FAILURE);
        }

        // print the converted data to the screen
        if (write(STDOUT_FILENO, buffer, bytes_read) != bytes_read) {
            perror("Error writing to stdout");
            exit(EXIT_FAILURE);
        }
    }

    // check for read error
    if (bytes_read == -1) {
        perror("Error reading from input file");
        exit(EXIT_FAILURE);
    }

    // close the input and output files
    if (close(input_fd) == -1) {
        perror("Error closing input file");
        exit(EXIT_FAILURE);
    }
    if (close(output_fd) == -1) {
        perror("Error closing output file");
        exit(EXIT_FAILURE);
    }

    return 0;
}
