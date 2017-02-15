#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "tinyshell.h"

/* from tinyshell.o's data segment */
extern int verbose;
extern char prompt[];	/* external array */
extern struct job_t jobs[MAXJOBS];

int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1;

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF)
    {
        switch (c)
        {
            case 'h':
                usage();
                break;
            case 'v':
                verbose = 1;
                break;
            case 'p':
                emit_prompt = 0;
                break;
            default:
                usage();
        }
    }
    init();
    /* Execute the shell's read/eval loop */
    while (1)
    {
        if (emit_prompt)
        {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin))    /* End of file (ctrl-d) */
        {
            fflush(stdout);
            exit(0);
        }

        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}