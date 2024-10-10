# mysh

This program is a basic Unix-like shell written in C, allowing for arbitrary input/output redirection and piping between processes.

## Usage:
With Make installed, user simply needs to type `make` in their system shell to generate an executable. 

To start the shell, run the executable 
```bash
$ ./mysh
```

To exit, user can type `exit` or press `CTRL + D`

## Limitations:
Shell does not support changing working directory -- program will always assume that working directory is the one in which program was executed.