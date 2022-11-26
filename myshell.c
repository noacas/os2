#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>

int prepare(void);
int process_arglist(int count, char **arglist);
int finalize(void);
int with_pipe(char **, int);
int with_background(char **, int);
int with_redirection(char **, int);
int execute_command(char **);
void child_execute_command(char ** arglist);
int parent_wait_for_child(pid_t pid);

const int NO_ERROR_EXIT = 1, ERROR_EXIT = 0, ERROR_RETURN_VALUE = -1, CHILD_ERROR_EXIT = 1, CHILD_NO_ERROR_EXIT = 0,
READ_SIDE = 0, WRITE_SIDE = 1, STD_SIDE_NOT_NEEDED = -1;

void avoid_zombies() {
    // eran's trick
    signal(SIGCHLD, SIG_IGN);
}

void set_to_ignore_sigint()
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, 0) == ERROR_RETURN_VALUE)
    {
        fprintf(stderr, "Failed setting to ignore sigint. %s\n", strerror(errno));
        exit(1);
    }
}

void set_to_not_ignore_sigint()
{
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, 0) == ERROR_RETURN_VALUE)
    {
        fprintf(stderr, "Failed setting to not ignore sigint. %s\n", strerror(errno));
        exit(1);
    }
}


int prepare(void) {
    avoid_zombies();
    set_to_ignore_sigint();
    return 0;
}

int finalize(void) {
    return 0;
}

int process_arglist(int count, char **arglist) {
    for (int i=0; i<count; i++) {
        if (arglist[i][0] == '|') {
            return with_pipe(arglist, i);
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
    pid_t pid = fork();
    if (pid == ERROR_RETURN_VALUE) {
        fprintf(stderr, "Failed created subprocess %s\n", strerror(errno));
        return ERROR_EXIT;
    }
    if (pid == 0) {
        // child
        child_execute_command(arglist);
    }
    // parent (child will never get here)
    return NO_ERROR_EXIT; // no need to wait for child
}

void child_execute_command(char ** arglist) {
    execvp(arglist[0], arglist);
    // from documentation: When execvp() is successful, it doesn't return; otherwise, it returns -1 and sets errno.
    fprintf(stderr, "Failed executing command %s %s\n", arglist[0], strerror(errno));
    exit(CHILD_ERROR_EXIT);
}

int parent_wait_for_child(pid_t pid) {
    int return_value, status;
    return_value = waitpid(pid, &status, WUNTRACED);
    // https://linux.die.net/man/2/waitpid documentation: because we use Eran's trick to avoid zombies
    // then the waitpid will act a bit different
    if (return_value == ERROR_RETURN_VALUE && errno != ECHILD && errno != EINTR) {
        fprintf(stderr, "Wait for child process %d failed with status code %d: %s\n", pid, status, strerror(errno));
        return ERROR_EXIT;
    }
    return NO_ERROR_EXIT;
}

int execute_command(char ** arglist) {
    pid_t pid = fork();
    if (pid == ERROR_RETURN_VALUE) {
        fprintf(stderr, "Failed created subprocess %s\n", strerror(errno));
        return ERROR_EXIT;
    }
    if (pid == 0) {
        // child
        set_to_not_ignore_sigint();
        child_execute_command(arglist);
    }
    // parent (child will never get here)
    return parent_wait_for_child(pid);
}

int pipe_handle(int my_pipe[2], int to_use_side, int std_side) {
    int other_side = (to_use_side + 1) % 2;
    // close other side of pipe
    if (close(my_pipe[other_side])  == ERROR_RETURN_VALUE) {
        printf("Failed closing side of the pipe. %s\n", strerror(errno));
        return CHILD_ERROR_EXIT;
    }
    // make std* go to pipe if needed
    if (std_side != STD_SIDE_NOT_NEEDED)
    {
        if (dup2(my_pipe[to_use_side], std_side) == ERROR_RETURN_VALUE) {
            printf("Failed getting std to be the pipe. %s\n", strerror(errno));
            return CHILD_ERROR_EXIT;
        }
    }
    // close use side of pipe
    if (close(my_pipe[to_use_side])  == ERROR_RETURN_VALUE) {
        printf("Failed closing side of the pipe. %s\n", strerror(errno));
        return CHILD_ERROR_EXIT;
    }
    return CHILD_NO_ERROR_EXIT;
}

int with_pipe(char ** arglist, int i) {
    int my_pipe[2];
    arglist[i] = NULL; // so that the executed command will not look at anything after |
    if (pipe(my_pipe) == ERROR_RETURN_VALUE) {
        fprintf(stderr, "Failed creating pipe. %s\n", strerror(errno));
        return ERROR_EXIT;
    }
    pid_t pid2, pid = fork();
    if (pid == ERROR_RETURN_VALUE) {
        fprintf(stderr, "Failed created subprocess %s\n", strerror(errno));
        return ERROR_EXIT;
    }
    if (pid == 0) {
        // child
        set_to_not_ignore_sigint();
        if (pipe_handle(my_pipe, WRITE_SIDE, STDOUT_FILENO) == CHILD_ERROR_EXIT)
            exit(CHILD_ERROR_EXIT);
        child_execute_command(arglist);
    }
    // parent (child will never get here)
    pid2 = fork();
    if (pid2 == ERROR_RETURN_VALUE) {
        fprintf(stderr, "Failed created subprocess %s\n", strerror(errno));
        return ERROR_EXIT;
    }
    if (pid2 == 0) {
        // second child
        set_to_not_ignore_sigint();
        if (pipe_handle(my_pipe, READ_SIDE, STDIN_FILENO) == CHILD_ERROR_EXIT)
            exit(CHILD_ERROR_EXIT);
        child_execute_command(arglist + (i + 1));
    }
    // parent (child will never get here)
    if (parent_wait_for_child(pid) == ERROR_EXIT) {
        return ERROR_EXIT;
    }
    // pipe_handle will close both sides
    if (pipe_handle(my_pipe, 0, STD_SIDE_NOT_NEEDED) == CHILD_ERROR_EXIT)
        return ERROR_EXIT;
    return parent_wait_for_child(pid2);
}

int with_redirection(char ** arglist, int redirection_index) {
    int file_descriptor;
    // the output file path is the next word in arglist after >
    char *output_file_path = arglist[redirection_index + 1];
    pid_t pid = fork();
    if (pid == ERROR_RETURN_VALUE) {
        fprintf(stderr, "Failed created subprocess %s\n", strerror(errno));
        return ERROR_EXIT;
    }
    if (pid == 0) {
        // child
        set_to_not_ignore_sigint();
        if ((file_descriptor = open(output_file_path, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU)) < 0) {
            printf("Failed creating file descriptor %s. %s\n", output_file_path, strerror(errno));
            exit(CHILD_ERROR_EXIT);
        }
        // make stdout go to file
        if (dup2(file_descriptor, STDOUT_FILENO) == ERROR_RETURN_VALUE) {
            printf("Failed duplicating file descriptor to stdout. %s\n", strerror(errno));
            exit(CHILD_ERROR_EXIT);
        }
        arglist[redirection_index] = NULL;
        child_execute_command(arglist);
    }
    // parent (child will never get here)
    return parent_wait_for_child(pid);
}
