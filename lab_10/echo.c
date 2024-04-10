#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

int main(void) {
    char input[256];

    struct timespec wait_ts;
    wait_ts.tv_sec = 2;
    wait_ts.tv_nsec = 0;

    fd_set main_set;
    FD_ZERO(&main_set);
    FD_SET(STDIN_FILENO, &main_set);

    fd_set loop_set;
    FD_ZERO(&loop_set);

    while (1) {
        loop_set = main_set;

        int result = pselect(1, &loop_set, NULL, NULL, &wait_ts, 0);

        if (result <= 0) {
            if (result < 0) {
                perror("[echo] pselect");
            } else {
                printf("input timed out\n");
            }

            break;
        }

        char* received = fgets(input, 255, stdin);

        if (received == NULL) {
            perror("[echo] fgets");
            break;
        }

        printf("given: %s", input);
    }
}
