CFLAGS=-Wall -pedantic

.PHONY: all
all: mysh

mysh: mysh.c
	gcc $(CFLAGS) -o mysh mysh.c

.PHONY: clean
clean:
	rm -f mysh 
