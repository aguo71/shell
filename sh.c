#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "./jobs.h"
#define BUFSIZE 1024
// Job ID and jobList global variables
int job = 1;
job_list_t *jobList;

/*
 * - Description:
 *      Fills the token and argv arrays by parsing the buffer character array
 * - Arguments:
 *      buffer: a char array representing user input
 *      argv: the argument array eventually used for execv() filled with buffer
 * tokens
 *      input: a redirected input if found output: a redirected output file if
 * found
 *      append: boolean representing if >> was the specified output redirect
 *      background: boolean representing if & was last character in input line
 */
void parse(char buffer[], char *argv[], char **input, char **output,
           int *append, int *background) {
    /*  Setup */
    int ctr = 0;
    char *buf = buffer;
    char *token;
    /*  Booleans indicating to tokenizer if next term should be file redirect */
    int lookInput = 0;
    int lookOutput = 0;
    /*  Checks for tokens in buffer and saves to argv
        unless token is a redirect symbol/redirect file
        and checks for syntax errors */
    while ((token = strtok(buf, " \t\n")) != NULL) {
        /*  Checks if previous token was an input or output redirect symbol
            and reacts appropriately */
        if (lookInput) {
            if (!strcmp(token, "<") || !strcmp(token, ">") ||
                !strcmp(token, ">>")) {
                fprintf(stderr,
                        "syntax error: input file is a redirection symbol\n");
                return;
            }
            if (*input != NULL) {
                fprintf(stderr, "syntax error: multiple input files\n");
                return;
            }
            *input = token;
            lookInput = 0;
            *background = 0;
        } else if (lookOutput) {
            if (!strcmp(token, "<") || !strcmp(token, ">") ||
                !strcmp(token, ">>")) {
                fprintf(stderr,
                        "syntax error: output file is a redirection symbol\n");
                return;
            }
            if (*output != NULL) {
                fprintf(stderr, "syntax error: multiple output files\n");
                return;
            }
            *output = token;
            lookOutput = 0;
            *background = 0;
        } else {
            *background = 0;
            if (!strcmp(token, "<")) {
                lookInput = 1;
            } else if (!strcmp(token, ">")) {
                lookOutput = 1;
            } else if (!strcmp(token, ">>")) {
                lookOutput = 1;
                *append = 1;
            } else {
                /*  This condition represents if we have found a command token
                    which is not a file redirect symbol/file so we save in argv
                    unless it is the & sign which we will ignore but set our
                    background boolean to be true
                 */
                if (!strcmp(token, "&")) {
                    *background = 1;
                } else {
                    *background = 0;
                    argv[ctr] = token;
                    ctr++;
                }
            }
        }
        buf = NULL;
    }
    /* Handles unused argument compiler warning */
    append = append;
    /* Post-tokenizing error handling */
    if (lookInput) {
        fprintf(stderr, "syntax error: no input file\n");
        return;
    }
    if (lookOutput) {
        fprintf(stderr, "syntax error: no output file\n");
        return;
    }
    if (!ctr && (*input != NULL || *output != NULL)) {
        fprintf(stderr, "redirects with no command\n");
        return;
    }
}

/*  Description:
        Function for changing directory while checking for errors
    Arguments:
        tokens: array of strings representing cd command and path */
void changeDir(char *tokens[]) {
    /* Checks for invalid number of arguments */
    if (tokens[2] != NULL || tokens[1] == NULL) {
        fprintf(stderr, "cd: syntax error\n");
        return;
    }
    int status = chdir(tokens[1]);
    if (status < 0) {
        perror("chdir");
        cleanup_job_list(jobList);
        exit(1);
    }
}

/*  Description:
        Function for adding a hard link to a file while checking for errors
    Arguments:
        tokens: array of strings representing ln command and files to be linked
 */
void addLink(char *tokens[]) {
    /* Checks for invalid number of arguments */
    if (tokens[3] != NULL || tokens[1] == NULL || tokens[2] == NULL) {
        fprintf(stderr, "ln: syntax error\n");
        return;
    }
    int status = link(tokens[1], tokens[2]);
    if (status < 0) {
        perror("link");
        cleanup_job_list(jobList);
        exit(1);
    }
}

/*  Description:
        Function for removing a link to a file while checking for errors
    Arguments:
        tokens: array of strings representing rm command and file */
void removeLink(char *tokens[]) {
    /* Checks for invalid number of arguments */
    if (tokens[2] != NULL || tokens[1] == NULL) {
        fprintf(stderr, "rm: syntax error\n");
        return;
    }
    int status = unlink(tokens[1]);
    if (status < 0) {
        perror("unlink");
        cleanup_job_list(jobList);
        exit(1);
    }
}

/*  Description:
        Function for exiting shell
    Arguments:
        tokens: array of strings representing exit command */
void exitHelper(char *tokens[]) {
    /* Checks for invalid number of arguments */
    if (tokens[1] != NULL) {
        fprintf(stderr, "exit: syntax error\n");
        return;
    }
    cleanup_job_list(jobList);
    exit(0);
}

/*  Description:
        Function for printing jobs list
    Arguments:
        tokens: array of strings representing jobs command */
void printJobs(char *tokens[]) {
    /* Checks for invalid number of arguments */
    if (tokens[1] != NULL) {
        fprintf(stderr, "jobs: syntax error\n");
        return;
    }
    jobs(jobList);
}

/*  Description:
        Function for resuming a job in foreground
    Arguments:
        tokens: array of strings representing fg command */
void fg(char *tokens[]) {
    /* Checks for invalid number of arguments */
    if (tokens[2] != NULL || tokens[1] == NULL) {
        fprintf(stderr, "fg: syntax error\n");
        return;
    }
    // Checks that second argument starts with %
    if (tokens[1][0] != '%') {
        fprintf(stderr, "fg: job input does not begin with %%\n");
        return;
    }
    // Converts job number to its process id
    int jobNum = atoi(tokens[1] + 1);
    pid_t jobPid = get_job_pid(jobList, jobNum);
    if (jobPid == -1) {
        fprintf(stderr, "job not found\n");
        return;
    }
    // Sets terminal control to input job
    if (tcsetpgrp(0, jobPid) == -1) {
        perror("tcsetpgrp");
        cleanup_job_list(jobList);
        exit(1);
    }
    // Continues job
    pid_t jobPgid;
    if ((jobPgid = getpgid(jobPid)) == -1) {
        perror("getpgid");
        cleanup_job_list(jobList);
        exit(1);
    }
    if (kill(-jobPgid, SIGCONT) == -1) {
        perror("kill");
        cleanup_job_list(jobList);
        exit(1);
    }
    // Waits for job to change status and responds accordingly
    int status;
    int waitReturn = waitpid(jobPid, &status, WUNTRACED);
    if (waitReturn == -1) {
        perror("waitpid");
        cleanup_job_list(jobList);
        exit(1);
    }
    // Checks waitpid status update and prints out informative message if
    // child process was terminated or suspended by a signal
    if (WIFSIGNALED(status)) {
        int signalNum = WTERMSIG(status);
        if (printf("[%d] (%d) terminated by signal %d\n", jobNum, jobPid,
                   signalNum) < 0) {
            fprintf(
                stderr,
                "Error: Could not print signal terminated process message.\n");
            return;
        }
        remove_job_pid(jobList, jobPid);
    } else if (WIFSTOPPED(status)) {
        int signalNum = WSTOPSIG(status);
        if (printf("[%d] (%d) suspended by signal %d\n", jobNum, jobPid,
                   signalNum) < 0) {
            fprintf(
                stderr,
                "Error: Could not print signal suspended process message.\n");
            return;
        }
    } else {
        // Removes job from jobList due to normal termination
        remove_job_pid(jobList, jobPid);
    }
    // Gets PID of main REPL (calling process) and sets it
    // back as controlling process group
    pid_t pgroup;
    if ((pgroup = getpgrp()) == -1) {
        perror("getpgrp");
        cleanup_job_list(jobList);
        exit(1);
    }
    if (tcsetpgrp(0, pgroup) == -1) {
        perror("tcsetpgrp");
        cleanup_job_list(jobList);
        exit(1);
    }
}

/*  Description:
        Function for resuming a job in the background
    Arguments:
        tokens: array of strings representing bg command */
void bg(char *tokens[]) {
    /* Checks for invalid number of arguments */
    if (tokens[2] != NULL || tokens[1] == NULL) {
        fprintf(stderr, "bg: syntax error\n");
        return;
    }
    // Checks that second argument starts with %
    if (tokens[1][0] != '%') {
        fprintf(stderr, "bg: job input does not begin with %%\n");
        return;
    }
    // Converts job number to its process id
    int jobNum = atoi(tokens[1] + 1);
    pid_t jobPid = get_job_pid(jobList, jobNum);
    if (jobPid == -1) {
        fprintf(stderr, "job not found\n");
        return;
    }
    // Continues job
    pid_t jobPgid;
    if ((jobPgid = getpgid(jobPid)) == -1) {
        perror("getpgid");
        cleanup_job_list(jobList);
        exit(1);
    }
    if (kill(-jobPgid, SIGCONT) == -1) {
        perror("kill");
        cleanup_job_list(jobList);
        exit(1);
    }
}

/*
 * - Description:
 *      Executes the program given in argv[0] with the subsequent parameters
 * given
 * - Arguments:
 *      argv: the tokenized argument array eventually used for execv()
 *      input: a redirected input if found
 *      output: a redirected output file if found
 *      append: boolean representing if >> was the specified output redirect
 *      background: boolean representing if & was last character in input line
 */
void execute(char *argv[], char *input, char *output, int append,
             int background) {
    // Saves a copy of full filepath of command
    char *filepath = argv[0];
    /* Creates child process */
    pid_t childPID;
    if ((childPID = fork()) == 0) {
        // Sets child PID as its PGID
        if (setpgid(0, 0) == -1) {
            perror("setpgid");
            cleanup_job_list(jobList);
            exit(1);
        }
        // Gets child PGID and sets it as controlling process group if its
        // a foreground process
        if (!background) {
            pid_t pgroup;
            if ((pgroup = getpgid(0)) == -1) {
                perror("getpgid");
                cleanup_job_list(jobList);
                exit(1);
            }
            if (tcsetpgrp(0, pgroup) == -1) {
                perror("tcsetpgrp");
                cleanup_job_list(jobList);
                exit(1);
            }
        }
        // Reinstates default signal handling behavior
        if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(jobList);
            exit(1);
        }
        if (signal(SIGTSTP, SIG_DFL) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(jobList);
            exit(1);
        }
        if (signal(SIGQUIT, SIG_DFL) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(jobList);
            exit(1);
        }
        if (signal(SIGTTOU, SIG_DFL) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(jobList);
            exit(1);
        }
        /* Converts path in argv[0] to just the last branch of path and saves in
         * argv[0] */
        char *path = strrchr(argv[0], '/');
        if (path != NULL && (path + 1) != NULL) {
            argv[0] = (path + 1);
        }
        /* Redirects file descriptor 0 to input */
        if (input != NULL) {
            if (close(0) == -1) {
                perror("close");
                cleanup_job_list(jobList);
                exit(1);
            }
            if (open(input, O_RDONLY) == -1) {
                perror("open");
                cleanup_job_list(jobList);
                exit(1);
            }
        }
        /* Redirects file descriptor 1 to output */
        if (output != NULL) {
            if (close(1) == -1) {
                perror("close");
                cleanup_job_list(jobList);
                exit(1);
            }
            /* Checks which of >> or > was specified and responds accordingly */
            if (append) {
                if (open(output, O_WRONLY | O_CREAT | O_APPEND, 0666) == -1) {
                    perror("open");
                    cleanup_job_list(jobList);
                    exit(1);
                }
            } else {
                if (open(output, O_WRONLY | O_CREAT | O_TRUNC, 0666) == -1) {
                    perror("open");
                    cleanup_job_list(jobList);
                    exit(1);
                }
            }
        }
        /*  Executes program in new process image with filepath being the full
            file path, and argv[0] now containing only the file binary name */
        execv(filepath, argv);
        /* We wonâ€™t get here unless execv failed, meaning an error occurred */
        perror("execv");
        cleanup_job_list(jobList);
        exit(1);
    }
    // Body of parent process:
    // Increments jobID counter if background process was forked and prints
    // jobID and processID of background job and adds it to job list
    if (background) {
        if (printf("[%d] (%d)\n", job, childPID) < 0) {
            fprintf(stderr, "Error: Could not print job and process id.\n");
            return;
        }
        add_job(jobList, job, childPID, RUNNING, filepath);
        job++;
    } else {
        /*  Waits for child to finish running before continuing unless supplied
            the background argument as 1, in which case it doesn't wait */
        int status;
        int waitReturn = waitpid(childPID, &status, WUNTRACED);
        if (waitReturn == -1) {
            perror("waitpid");
            cleanup_job_list(jobList);
            exit(1);
        }
        // Checks waitpid status update and prints out informative message if
        // child process was terminated or suspended by a signal
        if (WIFSIGNALED(status)) {
            int signalNum = WTERMSIG(status);
            if (printf("[%d] (%d) terminated by signal %d\n", job, childPID,
                       signalNum) < 0) {
                fprintf(stderr,
                        "Error: Could not print signal terminated process "
                        "message.\n");
                return;
            }
            job++;
        }
        if (WIFSTOPPED(status)) {
            int signalNum = WSTOPSIG(status);
            if (printf("[%d] (%d) suspended by signal %d\n", job, childPID,
                       signalNum) < 0) {
                fprintf(stderr,
                        "Error: Could not print signal suspended process "
                        "message.\n");
                return;
            }
            add_job(jobList, job, childPID, STOPPED, filepath);
            job++;
        }
        // Gets PID of main REPL (calling process) and sets it
        // back as controlling process group
        pid_t pgroup;
        if ((pgroup = getpgrp()) == -1) {
            perror("getpgrp");
            cleanup_job_list(jobList);
            exit(1);
        }
        if (tcsetpgrp(0, pgroup) == -1) {
            perror("tcsetpgrp");
            cleanup_job_list(jobList);
            exit(1);
        }
    }
}

/*  Description: sets up REPL as a command line for user input,
    parses these commands and executes while handling errors,
    exits upon control-D */
int main() {
    ssize_t status;
    // Initializes jobList
    jobList = init_job_list();
    /* REPL while loop */
    while (1) {
        /* Blocks signals in shell REPL */
        if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(jobList);
            exit(1);
        }
        if (signal(SIGTSTP, SIG_IGN) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(jobList);
            exit(1);
        }
        if (signal(SIGQUIT, SIG_IGN) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(jobList);
            exit(1);
        }
        if (signal(SIGTTOU, SIG_IGN) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(jobList);
            exit(1);
        }
        // Iterates through jobList and reaps terminated processes
        pid_t pid;
        while ((pid = get_next_pid(jobList)) != -1) {
            int status;
            int waitReturn =
                waitpid(pid, &status, WNOHANG | WUNTRACED | WCONTINUED);
            if (waitReturn == -1) {
                perror("waitpid");
                cleanup_job_list(jobList);
                exit(1);
            }
            // Checks waitpid status update and prints out informative message
            // if child process was terminated or updates status accordingly
            if (waitReturn != 0) {
                if (WIFEXITED(status)) {
                    int signalNum = WEXITSTATUS(status);
                    if (printf("[%d] (%d) terminated with exit status %d\n",
                               get_job_jid(jobList, pid), pid, signalNum) < 0) {
                        fprintf(stderr,
                                "Error: Could not print signal terminated "
                                "process message.\n");
                        cleanup_job_list(jobList);
                        exit(1);
                    }
                    remove_job_pid(jobList, pid);
                }
                if (WIFSIGNALED(status)) {
                    int signalNum = WTERMSIG(status);
                    if (printf("[%d] (%d) terminated by signal %d\n",
                               get_job_jid(jobList, pid), pid, signalNum) < 0) {
                        fprintf(stderr,
                                "Error: Could not print signal terminated "
                                "process message.\n");
                        cleanup_job_list(jobList);
                        exit(1);
                    }
                    remove_job_pid(jobList, pid);
                }
                if (WIFSTOPPED(status)) {
                    int signalNum = WSTOPSIG(status);
                    if (printf("[%d] (%d) suspended by signal %d\n",
                               get_job_jid(jobList, pid), pid, signalNum) < 0) {
                        fprintf(stderr,
                                "Error: Could not print signal suspended "
                                "process message.\n");
                        cleanup_job_list(jobList);
                        exit(1);
                    }
                    update_job_pid(jobList, pid, STOPPED);
                }
                if (WIFCONTINUED(status)) {
                    if (printf("[%d] (%d) resumed\n", get_job_jid(jobList, pid),
                               pid) < 0) {
                        fprintf(stderr,
                                "Error: Could not print process resumed "
                                "message.\n");
                        cleanup_job_list(jobList);
                        exit(1);
                    }
                    update_job_pid(jobList, pid, RUNNING);
                }
            }
        }
        /* Setup buffer */
        char buffer[BUFSIZE];
        for (int i = 0; i < 1024; i++) {
            buffer[i] = '\0';
        }
/* Handles PROMPT flag and displays the command-line prompt */
#ifdef PROMPT
        if (printf("33sh> ") < 0) {
            fprintf(stderr, "Error: Could not print REPL prompt in terminal\n");
        }
        if (fflush(stdout) < 0) {
            perror("fflush");
            cleanup_job_list(jobList);
            exit(1);
        }
#endif
        /*  Reads stdin input to buffer for parsing, handles errors, and
            exits while loop upon control-D */
        status = read(0, buffer, 1024);
        if (status > 0) {
            /* Sets up arguments for parse */
            char *argv[status];
            char *input = NULL;
            char *output = NULL;
            int append = 0;
            int background = 0;
            for (int i = 0; i < status; i++) {
                argv[i] = NULL;
            }
            /* Calls parse */
            parse(buffer, argv, &input, &output, &append, &background);
            if (argv[0] == NULL) {
                continue;
            }
            /* Checks for built-in calls */
            if (!strcmp(argv[0], "cd")) {
                changeDir(argv);
            } else if (!strcmp(argv[0], "ln")) {
                addLink(argv);
            } else if (!strcmp(argv[0], "rm")) {
                removeLink(argv);
            } else if (!strcmp(argv[0], "exit")) {
                exitHelper(argv);
            } else if (!strcmp(argv[0], "jobs")) {
                printJobs(argv);
            } else if (!strcmp(argv[0], "fg")) {
                fg(argv);
            } else if (!strcmp(argv[0], "bg")) {
                bg(argv);
            } else {
                /* Calls function to fork a child to run command */
                execute(argv, input, output, append, background);
            }
        } else if (status == -1) {
            perror("read");
            cleanup_job_list(jobList);
            exit(1);
        } else {
            break;
        }
    }
    cleanup_job_list(jobList);
    return 0;
}