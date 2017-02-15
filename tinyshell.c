#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include "tinyshell.h"

char prompt[] = "tsh> ";
int verbose = 0;
int nextjid = 1;
char sbuf[MAXLINE];

struct job_t jobs[MAXJOBS];

static int parseline(const char *cmdline, char **argv);
static int builtin_cmd(char **argv);
static void do_bgfg(char **argv);
static void waitfg(pid_t pid);
static void clearjob(struct job_t *job);
static int maxjid(struct job_t *jobs);
static int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
static int deletejob(struct job_t *jobs, pid_t pid);
static pid_t fgpid(struct job_t *jobs);
static struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
static struct job_t *getjobjid(struct job_t *jobs, int jid);
static int pid2jid(pid_t pid);
static void listjobs(struct job_t *jobs);
static void unix_error(char *msg);
static void sigchld_handler(int sig);
static void sigint_handler(int sig);
static void sigtstp_handler(int sig);
static void sigquit_handler(int sig);
static handler_t *Signal(int signum, handler_t *handler);
static int search_dir(const char* path, const char* fn);
static char* search_path_variable(const char* fn);
static char* search_env_variable(const char* var_name, const char* fn);

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
static int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* at data segment */
    char *buf = array;
    char *delim;
    int argc;
    int bg;

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';   /* final '\n' to ' ' */
    while (*buf && (*buf == ' '))
        buf++;

    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' '))
            buf++;

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    if (argc == 0)  /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
        argv[--argc] = NULL;
    }
    return bg;
}

static int search_dir(const char* path, const char* fn) {
    struct dirent *p_dirent;
    DIR* dir = opendir(path);
    size_t size = strlen(fn);
    if(dir == NULL) {
        return -1;  /* Can't open it */
    }
    while((p_dirent = readdir(dir)) != NULL) {
        if(memcmp(p_dirent->d_name, fn, size) == 0 &&
            p_dirent->d_name[size] == '.') {
            return 1;   /* Found */
        }
    }
    closedir(dir);
    return 0;   /* Not found */
}

static char* search_env_variable(const char* var_name, const char* fn) {
    static char env_var[MAX_VAR_LEN];
    char* s = getenv(var_name);
    char* p = s;
    char* t = env_var;

    while(*p && *p == ' ') p++;

    while(*p != '\0') {
        while(*p && *p != ':') {
            *t++ = *p++;
        }
        *t = '\0';
        if(search_dir(env_var, fn) > 0) {
            return env_var;
        }
        p++;
        while(*p && *p == ' ') p++;
        t = env_var;
    }
    return NULL;
}

static char* search_path_variable(const char* fn) {
    static char path_var[MAX_VAR_LEN];
    char* s = getenv("PATH");
    char* p = s;
    char* t = path_var;

    while(*p && *p == ' ') p++;

    while(*p != '\0') {
        while(*p && *p != ':') {
            *t++ = *p++;
        }
        *t = '\0';
        if(search_dir(path_var, fn) > 0) {
            return path_var;
        }
        p++;
        while(*p && *p == ' ') p++;
        t = path_var;
    }
    return NULL;
}

static int builtin_cmd(char **argv) {
    if (strcmp(argv[0], "quit") == 0) {
        exit(EXIT_SUCCESS);
    }
    else if (strcmp(argv[0], "jobs") == 0) {
        listjobs(jobs);
        return 1;
    }
    else if (strcmp(argv[0], "bg") == 0 || strcmp(argv[0], "fg") == 0) {
        do_bgfg(argv);
        return 1;
    }
    else {
        return 0;
    }
}

/* fg/bg ([pid]|[%jid]) */
static void do_bgfg(char **argv) {
    int  error_code;
    int  id;
    int is_jid;
    int bg;
    struct job_t *job;
    char *buf = argv[1];
    bg = (strcmp(argv[0], "bg") == 0);

    /* check validity of argv and process the request */
    do {
        /* we should have two arguments */
        if (argv[1] == NULL) {
            error_code = 3;
            break;
        }

        is_jid = (*buf == '%');
        if (is_jid) buf++;

        if (!isdigit(buf[0])) {
            error_code = 2; break;
        }
        id = atoi(buf);

        /* find the job */
        if (is_jid) {
            job = getjobjid(jobs, id);
            if (job == NULL) {
                error_code = 0; break;
            }
        }
        else {
            job = getjobpid(jobs, id);
            if (job == NULL) {
                error_code = 1; break;
            }
        }

        /* resume if needed */
        if (job->state == ST) {
            if(kill(-(job->pid), SIGCONT) == -1)
                unix_error("kill");
        }
        job->state = (bg ? BG : FG);

        if (bg)
            printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);

        return;
    } while (0);

    switch (error_code) {
        case 0:
            fprintf(stderr, "%s: No such job\n", argv[1]);
            break;
        case 1:
            fprintf(stderr, "(%s): No such process\n", argv[1]);
            break;
        case 2:
            fprintf(stderr, "%s: argument must be a PID or %%jobid\n", argv[0]);
            break;
        case 3:
            fprintf(stderr, "%s command requires PID or %%jobid argument\n", argv[0]);
            break;
    }
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
static void waitfg(pid_t pid) {
    /* "wait" but not reap - using busy loops */
    while (fgpid(jobs) == pid)
        sleep(0);
}

static void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

static void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

static int maxjid(struct job_t *jobs) {
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

static int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

static int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs)+1;
            return 1;
        }
    }
    return 0;
}

static pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

static struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

static struct job_t *getjobjid(struct job_t *jobs, int jid) {
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

static int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

static void listjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG:
                    printf("Running ");
                    break;
                case FG:
                    printf("Foreground ");
                    break;
                case ST:
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ",
                            i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}

/*
 * unix_error - unix-style error routine
 */
static void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
static void sigchld_handler(int sig) {
    pid_t pid;
    int   status;
    struct job_t *job;

    /* more than one children can be defunct / stopped */
    while ((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0) {
        job = getjobpid(jobs, pid);

        /* job stopped or terminated */
        if (WIFSTOPPED(status)) {
            /* stopped - message it and change status to ST */
            printf("Job [%d] (%d) stopped by signal 20\n", job->jid, job->pid);
            job->state = ST;
        }
        else {
            /* message if it was terminated by signal */
            if (WIFSIGNALED(status))
                printf("Job [%d] (%d) terminated by signal 2\n", job->jid, job->pid);

            /* terminated - delete from job list */
            deletejob(jobs, pid);
        }
    }

    /* exited while loop by error */
    if (pid == -1 && errno != ECHILD)
        unix_error("waitpid");
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
static void sigint_handler(int sig) {
    pid_t pid;

    pid = fgpid(jobs);
    if (pid == 0)
        return;

    if (kill(-pid, SIGINT) == -1)
        unix_error("kill");
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
static void sigtstp_handler(int sig) {
    pid_t pid;

    pid = fgpid(jobs);
    if (pid == 0)
        return;

    if (kill(-pid, SIGTSTP) == -1)
        unix_error("kill");
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
static void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
static handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

void init() {
    /* Install the signal handlers */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    initjobs(jobs);
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */
void eval(char *cmdline) {
    char *argv[MAXARGS];
    int  bg;
    pid_t pid;
    pid_t fg_pid;           /* pid of foreground job (if any) */
    sigset_t set;
    char cwd[128];
    char* dir;
    int dir_size;

    bg = parseline(cmdline, argv);

    if (argv[0] == NULL)
        return;

    /* when input is NOT a built-in command... */
    if (builtin_cmd(argv) == 0) {
        /* ignore SIGCHLD from child process that is not a 'job' */
        if (sigemptyset(&set) == -1)
            unix_error("sigemptyset");
        if (sigaddset(&set, SIGCHLD) == -1)
            unix_error("sigaddset");
        if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
            unix_error("sigprocmask");

        switch (pid = fork()) {
            case -1:
                unix_error("fork");
            case 0:
                /* unblock */
                if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1)
                    unix_error("sigprocmask");

                /* assign new process group */
                if (setpgid(0, 0) == -1)
                    unix_error("setpgid");

                /* execute requested program (new process) */
                if((getcwd(cwd, sizeof(cwd))) == NULL)
                    fprintf(stdout, "Can't open current directory.\n");
                if(search_dir(cwd, argv[0]) > 0) {
                    execv(argv[0], argv);
                } else {
                    if((dir = search_env_variable("PATH", argv[0])) != NULL) {
                        dir_size = strlen(dir);
                        memcpy(cwd, dir, dir_size);
                        cwd[dir_size] = '/';
                        memcpy(cwd+dir_size+1, argv[0], strlen(argv[0]));
                        cwd[dir_size+1+strlen(argv[0])] = '\0';
                        execv(cwd, argv);
                    }
                }

                /* flow reaches here when execv fails */
                if (errno == ENOENT) {
                    fprintf(stderr, "%s: Command not found\n", argv[0]);
                    exit(EXIT_FAILURE);
                }
                else {
                    unix_error("execv");
                }
        }

        addjob(jobs, pid, (bg ? BG : FG), cmdline);

        /* unblock */
        if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1)
            unix_error("sigprocmask");

        /* message that background process has started */
        if (bg)
            printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
    }

    /* wait for a foreground job gets done or suspended (if any) */
    if ((fg_pid = fgpid(jobs)))
        waitfg(fg_pid);
}

/*
 * usage - print a help message
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}
