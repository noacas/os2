#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

int prepare(void);
int process_arglist(int count, char **arglist);
int finalize(void);
int with_pipe(char **, int, int);
int with_background(char **, int);
int with_redirection(char **, int);
int execute_command(char **);

const int NO_ERROR_EXIT = 1, ERROR_EXIT = 0, ERROR_RETURN_VALUE = -1;

/* TODO:
 * 1. find a way to avoid zombies that wont interrupt with waiting for the other processes
 */

int prepare(void) {
    return 0;
}

int finalize(void) {
    return 0;
}

int process_arglist(int count, char **arglist) {
    for (int i=0; i<count; i++) {
        if (arglist[i][0] == '|') {
            return with_pipe(arglist, i, count);
        }
        else if (arglist[i][0] == '&') {
            return with_background(arglist, i);
        }
        else if (arglist[i][0] == '>') {
            return with_redirection(arglist, i);
        }
    }
    return execute_command(arglist);
}

int with_background(char ** arglist, int last_index) {
    arglist[last_index] = NULL; // we want to remove & from the command
    signal(SIGCHLD, SIG_IGN); // Eran's trick to avoid zombies
    pid_t pid = fork();
    switch (pid) {
        case ERROR_RETURN_VALUE:
            fprintf(stderr, "Failed created child process %s\n", strerror(errno));
            return ERROR_EXIT;
        case 0:
            // child
            execvp(arglist[0], arglist);
            // from documentation: When execvp() is successful, it doesn't return; otherwise, it returns -1 and sets errno.
            fprintf(stderr, "Failed executing command %s %s\n", arglist[0], strerror(errno));
            exit(ERROR_EXIT);
        default:
            // parent
            return NO_ERROR_EXIT; // no need to wait for child
    }
}

int execute_command(char ** arglist) {
    pid_t pid = fork();
    int return_value, status;
    switch (pid) {
        case ERROR_RETURN_VALUE:
            fprintf(stderr, "Failed created subprocess %s\n", strerror(errno));
            return ERROR_EXIT;
        case 0:
            // child
            return_value = execvp(arglist[0], arglist);
            // from documentation: When execvp() is successful, it doesn't return; otherwise, it returns -1 and sets errno.
            fprintf(stderr, "Failed executing command %s %s\n", arglist[0], strerror(errno));
            exit(ERROR_EXIT);
        default:
            // parent
            return_value = waitpid(pid, &status, 0);
            if (return_value == ERROR_RETURN_VALUE) {
                fprintf(stderr, "Failed waiting for child process %d %s\n", pid, strerror(errno));
                return ERROR_EXIT;
            }
            if (0 == status || ECHILD == status || EINTR == status) {
                printf("Child process %d executed the command %s\n", pid, arglist[0]);
            } else {
                fprintf(stderr, "Child process %d finished with an error code %d: %s\n", pid, status, strerror(errno));
            }
            return NO_ERROR_EXIT;
    }
}

int with_pipe(char ** arglst, int i, int count) {
    return 0;
}
int with_redirection(char ** arglist, int redirection_index) {
    int file_descriptor, return_value, status;
    // the output file path is the next word in arglist after >
    char *output_file_path = arglist[redirection_index + 1];
    pid_t pid = fork();
    switch (pid) {
        case ERROR_RETURN_VALUE:
            fprintf(stderr, "Failed created subprocess %s\n", strerror(errno));
            return ERROR_EXIT;
        case 0:
            // child
            if ((file_descriptor = open(output_file_path, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU)) < 0) {
                printf("Failed creating file descriptor %s. %s\n", output_file_path, strerror(errno));
                exit(ERROR_EXIT);
            }
            // make stdout go to file
            if (dup2(file_descriptor, 1) == ERROR_RETURN_VALUE) {
                printf("Failed duplicating file descriptor to stdout. %s\n", strerror(errno));
                exit(ERROR_EXIT);
            }
            arglist[redirection_index] = NULL;
            return_value = execvp(arglist[0], arglist);
            // from documentation: When execvp() is successful, it doesn't return; otherwise, it returns -1 and sets errno.
            fprintf(stderr, "Failed executing command %s %s\n", arglist[0], strerror(errno));
            exit(ERROR_EXIT);
        default:
            // parent
            return_value = waitpid(pid, &status, 0);
            if (return_value == ERROR_RETURN_VALUE) {
                fprintf(stderr, "Failed waiting for child process %d %s\n", pid, strerror(errno));
                return ERROR_EXIT;
            }
            if (0 == status || ECHILD == status || EINTR == status) {
                printf("Child process %d executed the command %s\n", pid, arglist[0]);
            } else {
                fprintf(stderr, "Child process %d finished with an error code %d: %s\n", pid, status, strerror(errno));
            }
            return NO_ERROR_EXIT;
    }
}
