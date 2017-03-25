#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#define MAXCMDS 32
#define CMDLEN 64

typedef struct {
    int size, n;
    char* buff, *file;
    char** args;
} cmd_t;

void args2str(int argc, char** argv, char* buff) {
    if(argc == 0) return;
    int i;

    char* p = buff;
    size_t len;
    for(i = 0; i < argc; i++) {
        len = strlen(argv[i]);
        memcpy(p, argv[i], len);
        p += len;
        *p++ = ' ';
    }
    p[-1] = '\0';
}

const char* fn_end(const char* p) {
    while(*p && *p != ' ') p++;
    return p;
}

int count_args(const char* p) {
    while(*p && *p == ' ') p++;
    char c;
    int count = 0;
    while((c = *p)) {
        if(c == ' ') {
            count++;
            while(*p && *p == ' ') p++;
        } else {
            p++;
        }
    }
    
    return count+1;
}

void parse_args(char** args, int n, const char* ptr) {
    const char* d;
    const char* p = ptr;
    int len = 0;
    const char* total = ptr + strlen(ptr);
    for(int i = 0; i < n; i++) {
        d = strchr(p, ' '); // if no space found, then return 0.
        if(d != 0) {
            len = d - p;
        } else {
            len = total - p;
        }
        args[i] = (char*)malloc(sizeof(char)*(len+1));
        memcpy(args[i], p, len);
        args[i][len] = '\0';
        p = d+1;
    }
    args[n] = NULL;
}

void print_args(char** cmd) {
    while(*cmd) {
        printf("\targ. %s\n", *cmd);
        cmd++;
    }
}

void alloc_fill(char** dest, const char* src, int len) {
    int i = len-1;
    while(src[i]==' ') i--;
    len = i + 1;
    *dest = (char*)malloc(sizeof(char)*(len+1));
    memcpy(*dest, src, len);
    (*dest)[len] = '\0';
}

// return the number of parsed cmd
int parse(cmd_t* cmds, const char* cmdline) {
    const char* p = cmdline;
    const char* ptr = cmdline;
    char c;
    int len, i = 0;
    while(1) {
        c = *p++;
        if(c == '|' || c == '\0') {
            while(*ptr == ' ') ptr++;
            len = p - ptr - 1;
            alloc_fill(&(cmds[i].buff), ptr, len);
            // printf("buff: %s\n", cmds[i].buff);

            const char* file = fn_end(ptr);
            alloc_fill(&(cmds[i].file), ptr, file-ptr);
            // printf("file: %s\n", cmds[i].file);

            int num_args = count_args(cmds[i].buff);
            // printf("args_num: %d\n", num_args);
            cmds[i].n = num_args;
            cmds[i].args = (char**)malloc(sizeof(char*)*(num_args+1));
            parse_args(cmds[i].args, num_args, ptr);
            // print_args(cmds[i].args);

            ptr = p;
            i++;
            if(c == '\0') break;
        }
    }
    return i;
}

int main (int argc, char ** argv) {
    int i, n;
    char* cmdline;
    char* cmds[] = {"ls -l", "|", "grep main", "|", "grep 14"};
    cmd_t tcmds[MAXCMDS];

    cmdline = (char*)malloc(sizeof(char)*256);  // cmdline buffer;
    args2str(5, cmds, cmdline);
    fprintf(stdout, "%s\n", cmdline);

    n = parse(tcmds, cmdline);
    for(int j = 0; j < n; j++) {
        printf("file %d: %s\n", j, tcmds[j].file);
        print_args(tcmds[j].args);
    }
    char* args[] = {"ls", "-l", NULL};
    // execvp("ls", args);

    // execlp("ls", "ls", "-l", (char*)NULL);

    char* args0[] = {"grep", "main", NULL};

    for(i = 0; i < n-1; i++) {
        int pd[2];
        pipe(pd);
        if(!fork()) {
            close(pd[0]);
            dup2(pd[1], 1);
            // execvp("ls", args);
            execvp(tcmds[i].file, tcmds[i].args);
            perror("exec");
            abort();
        }
        dup2(pd[0], 0);
        close(pd[1]);
    }

    // execvp("grep", args0);
    execvp(tcmds[i].file, tcmds[i].args);
    perror("exec");
    abort();

    free(cmdline);
    for(int i = 0; i < n; i++) {
        free(tcmds[i].buff);
    }

    // for( i=1; i<argc-1; i++)
    // {
    //     /* parent and child both have the double pd (file descriptor, so remember to close one) */
    //     int pd[2];
    //     pipe(pd);   /* pd[0] refers to the read end, pd[1] refers to the write end */

    //     /* child process */
    //     if (!fork()) {
    //         close(pd[0]);   // child close its read end
    //         dup2(pd[1], 1); // remap output back to parent
    //         execlp(argv[i], argv[i], NULL);
    //         perror("exec");
    //         abort();
    //     }

    //     // remap output from previous child to input
    //     dup2(pd[0], 0);
    //     close(pd[1]);
    // }

    // execlp(argv[i], argv[i], NULL);
    // perror("exec");
    // abort();
}
