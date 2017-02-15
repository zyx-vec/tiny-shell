#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main (int argc, char ** argv) {
    int i;

    for( i=1; i<argc-1; i++)
    {
        /* parent and child both have the double pd (file descriptor, so remember to close one) */
        int pd[2];
        pipe(pd);   /* pd[0] refers to the read end, pd[1] refers to the write end */

        /* child process */
        if (!fork()) {
            close(pd[0]);   // child close its read end
            dup2(pd[1], 1); // remap output back to parent
            execlp(argv[i], argv[i], NULL);
            perror("exec");
            abort();
        }

        // remap output from previous child to input
        dup2(pd[0], 0);
        close(pd[1]);
    }

    execlp(argv[i], argv[i], NULL);
    perror("exec");
    abort();
}
