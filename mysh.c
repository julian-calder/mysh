/*
 * mysh.c
 *
 * homemade shell implementation, capable of piping, input redirection, and running an arbitrary number of programs at the same time
 *
 * utilizing several commands:
 * fork(2) -- lets us split parent process into an identical child
 * exec(2) -- lets us begin new process within child
 * wait(2) -- lets parent process wait until child is finished
 * pipe(2) -- lets us make a channel between two file descriptors
 * dup(2) -- lets us reassign file descriptors
 * fgets(3) -- lets us take user input
 * strtok(3) -- helps with string parsing
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_INPUT_LEN 4096
#define MAX_ARGS 10

void print_prompt();
void process_args(char *arg_str, int read_fd, int write_fd);
int count_pipes(char *input_str);
int count_redirs(char *arg_str);

int
main(int argc, char *argv[])
{
    char input_buf[MAX_INPUT_LEN];
    char counting_buf[MAX_INPUT_LEN];
    int fds[2];
    int num_pipes, pipe_index, read_fd, write_fd, prev_read_fd; 
    pid_t child_pid;

    print_prompt();

    while(fgets(input_buf, MAX_INPUT_LEN, stdin) != NULL) {
        // duplicate input into buffer which we can use to count the number of pipes
        strcpy(counting_buf, input_buf);
        // count number of pipes in our input
        num_pipes = count_pipes(counting_buf);

        // by default, these are the standard file descriptors
        write_fd = 1;
        read_fd = 0;

        // iterate through the pipes of our input, even if there are 0
        char *token = strtok(input_buf, "|");

        // store each input separated by the pipes
        pipe_index = 0;

        // iterate through the input string
        while(token != NULL) {
            // if user types exit, exit with code EXIT_SUCCESS (0)
            if (strcmp(token, "exit\n") == 0) {
                exit(EXIT_SUCCESS);
            }
            // if user doesn't type anything
            if (strcmp(token, "\n") == 0) {
                break;
            }

            // update previous read_fd from previous pipe
            // handles case even when there are no pipes
            prev_read_fd = read_fd;

            // if we need to pipe
            if (pipe_index < num_pipes) {
                // make the pipe
                if (pipe(fds) < 0) {
                    perror("pipe");
                    break;
                }

                // store pipe file desciptors
                read_fd = fds[0];
                write_fd = fds[1];
            }

            // fork into child process
            if ((child_pid = fork()) < 0) {
                perror("fork");
                exit(1);
            }
            
            // if we are in the child
            if (child_pid == 0) {
                // if we are in the first argument
                if (pipe_index == 0) {
                    // there can be no preceding pipe so read fd is stdin (and could be input from file)
                    read_fd = 0;
                    process_args(token, read_fd, write_fd);
                }
                else {
                    if (pipe_index == num_pipes) {
                        write_fd = 1;
                    }
                    process_args(token, prev_read_fd, write_fd);
                }
            }
            // if we are in the parent
            else {
                // check to see if we have opened any pipes 
                if (num_pipes > 0) {
                    if (pipe_index < num_pipes) {
                        // if we have, we can close the write pipe because it has been passsed to child
                        if (close(write_fd) < 0) {
                            perror("close");
                            break;
                        }
                    }
                    // if we are anywhere but the first argument, close the preceding pipe because
                    // it has been passed to child
                    if (pipe_index != 0) {
                        if (close(prev_read_fd) < 0) {
                            perror("close");
                            break;
                        }
                    }
                }
                // get the next token and increase our pipe index
                token = strtok(NULL, "|");
                pipe_index++;
            }
        }

        // wait for children to finish before we print another shell prompt 
        for (int i = 0; i < num_pipes + 1; i++) {
            wait(NULL);
        }
        print_prompt();
    }
    return 0;
}

void
print_prompt()
{
    printf("$ ");
}

int
count_pipes(char *input_buf) 
{
    /*
     * helper function to count the number of pipes in a given command typed into the shell
     *
     * args: 
     *  char *input_buf: pointer to buffer storing the entire string user typed into shell
     * 
     * returns:
     *  integer number of pipes in command supplied to shell
     */

    int pipe_count = 0;
    char *token = strtok(input_buf, " ");

    // iterate through input string. every time
    // we see a pipe increase our pipe count by 1
    while(token != NULL) {
        if (strcmp(token, "|") == 0) {
            pipe_count += 1;
        }
        token = strtok(NULL, " ");
    }
    return pipe_count;
}

void
process_args(char *arg_str, int read_fd, int write_fd) 
{
    /* 
     * helperfunction called within child process to process a single entry to our shell 
     * (what would be separated by pipes if there were any).
     *
     * args:
     *  char *arg_str: pointer to buffer storing a single pipe-separated user entry to our shell
     *  int read_fd: file descriptor reflecting where data could be read in from to this process (stdin or from a pipe) 
     *  int write_fd: file descriptor reflecting where data could be written to in this process (stdout or to a pipe)
     */

    char *opt_args_buf[MAX_ARGS];
    char *program_name, *input_file, *output_file;
    int fd, args_buf_index;

    // initialize the index of our argument buffer
    args_buf_index = 0;

    // replace stdout with our pipe (potentially)
    // could also just be 1
    if (write_fd != 1) {
        if (dup2(write_fd, 1) < 0) {
            perror("dup2");
            exit(2);
        }
        if (close(write_fd) < 0) {
            perror("close");
            exit(1);
        }
    }

    // replace stdin with our pipe (potentially)
    // could also just be 0
    if (read_fd != 0) {
        if (dup2(read_fd, 0) < 0) {
            perror("dup2");
            exit(2);
        }
        if (close(read_fd) < 0) {
            perror("close");
            exit(1);
        }
    }

    char *token = strtok(arg_str, " ");

    // program to be called will always be first token, so save it
    program_name = token;

    // iterate through our argument, either adding to our args_buf array
    // or handling various redirection cases <, >, or >>
    while(token != NULL) {
        if (strcmp(token, "exit\n") == 0) {
            exit(EXIT_SUCCESS);
        }

        // input redirection
        if (strcmp(token, "<") == 0) {
            input_file = strtok(NULL, " ");
            // remove trailing newline character, if there is one
            input_file[strcspn(input_file, "\n")] = 0;

            if ((fd = open(input_file, O_RDONLY)) < 0) {
                perror("open");
                exit(1);
            }

            if (dup2(fd, 0) < 0) {
                perror("dup2");
                exit(2);
            }

            // make sure that final entry in args buf is NULL
            opt_args_buf[args_buf_index] = NULL;
        }

        // output redirection
        else if (strcmp(token, ">") == 0) {
            output_file = strtok(NULL, " ");
            // remove trailing newline character, if there is one
            output_file[strcspn(output_file, "\n")] = 0;

            if ((fd = open(output_file, O_TRUNC | O_CREAT | O_WRONLY, 0644)) < 0) {
                perror("open");
                exit(1);
            }

            if (dup2(fd, 1) < 0) {
                perror("dup2");
                exit(2);
            }

            // make sure that final entry in args buf is NULL
            opt_args_buf[args_buf_index] = NULL;
        }

        // output redirection with appending
        else if (strcmp(token, ">>") == 0) {
            output_file = strtok(NULL, " ");
            // remove trailing newline character, if there is one
            output_file[strcspn(output_file, "\n")] = 0;

            if ((fd = open(output_file, O_APPEND | O_CREAT | O_WRONLY, 0644)) < 0) {
                perror("open");
                exit(1);
            }

            if (dup2(fd, 1) < 0) {
                perror("dup2");
                exit(2);
            }

            // make sure that final entry in args buf is NULL
            opt_args_buf[args_buf_index] = NULL;
        }
        else {
            opt_args_buf[args_buf_index] = token;
            // remove trailing newline character, if there is one
            opt_args_buf[args_buf_index][strcspn(opt_args_buf[args_buf_index], "\n")] = 0;

            args_buf_index++;
        }
        token = strtok(NULL, " ");
    }
    // remove trailing newline character, if there is one
    program_name[strcspn(program_name, "\n")] = 0;

    // make sure that final entry in args buf is NULL
    opt_args_buf[args_buf_index] = NULL;
    if (execvp(program_name, opt_args_buf) < 0) {
        perror("execvp");
        exit(3);
    };
}
