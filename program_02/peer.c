#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h> 

// Program_02 - Peer-to-Peer Introduction
// Madison Webb and David Cathers

join() {

}

publish() {

}

search() {
    printf("Enter a file name: \n");
}

exit() {

}

print_options() {
    char selection[10];
    char selection[10];

    printf("Enter a command: \n");
    printf("JOIN: sends a JOIN request to the registry.\n");
    printf("PUBLISH: send a PUBLISH request to the registry.\n");
    printf("SEARCH: reads a file name from the terminal, print peer info.\n");
    printf("EXIT: close the peer application.\n");
    
    scanf("%s", selection);

    if (selection == "JOIN") {
        join();
    } else if (selection == "PUBLISH") {
        publish();
    } else if (selection == "SEARCH") {
        search();
    } else {
        exit();
    }

    return 0;
}

int main(int argc, char *argv[]) {
    print_options();
}