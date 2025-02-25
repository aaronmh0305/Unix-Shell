#####################################################################
#
# mysh.c is the name of your source code; you may change this.
# However, you must keep the name of the executable as "mysh".
#
# Type "make" or "make mysh" to compile your code
# 
# Type "make clean" to remove the executable (and any object files)
#
#####################################################################

CC=gcc
CFLAGS=-Wall -Werror

mysh: mysh.c
	$(CC) -o mysh $(CFLAGS) mysh.c

output: output.c
	$(CC) -o output $(CFLAGS) output.c

clean:
	$(RM) mysh 
