/* Copyright Aaron Hernandez 2019 */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_LEN 512
#define MAX_JOBS 32

// GLOBALS
FILE * fileInput;  // the file for batch mode, or stdin for interactive
int isBatchMode = 0;       // determines if this is batch mode
int jobId = -1;            // the job id for this process

/* Stored data for each individual process in linked list */
struct ProcInfo {
     int jid;
     int pid;
     char line[MAX_LEN];
     struct ProcInfo * next;
};

// stores the head of the linked list
struct ProcInfo * head = NULL;

/* Removes a terminated background process */
void removeBgProcess(int pid) {
    struct ProcInfo * curr = head;
    struct ProcInfo * prev = NULL;

    // If head node is to be removed
    if (curr != NULL && curr->pid == pid) {
        head = head->next;
        free(curr);
        curr = NULL;
        return;
    }

    // search for the node to remove
    while (curr != NULL && curr->pid != pid) {
        prev = curr;
        curr = curr->next;
    }

    // if the node wasn't found
    if (curr == NULL) {
        return;
    }

    // remove the node
    prev->next = curr->next;
    free(curr);
}

/* Adds a background process to the end of the linked list */
void addBgProcess(struct ProcInfo * n) {
    if (head == NULL) {
       head = n;
       return;
    }

    struct ProcInfo * current = head;
    while (current->next != NULL) {
        current = current->next;
    }

    current->next = n;
}

/* Frees memory when program terminates */
void freeResources() {
    struct ProcInfo * current;
    while (head != NULL) {
        current = head;
        head = head->next;
        free(current);
        current = NULL;
    }
}

void waitCommand(char * num) {
    char temp[1000];
    snprintf(temp, sizeof(temp), "Invalid JID %s\n", num);

    int numLen = strlen(num);
    for (int i = 0; i < numLen; i++) {
        if (num[i] < '0' || num[i] > '9') {
            write(STDERR_FILENO, temp, strlen(temp));
            return;
        }
    }

    int userJid = atoi(num);
    if (userJid > jobId) {
        write(STDERR_FILENO, temp, strlen(temp));
        return;
    }

    struct ProcInfo * current = head;
    while (current != NULL) {
        if (current->jid == userJid) {
            int status;
            waitpid(current->pid, &status, WCONTINUED | WUNTRACED);
            snprintf(temp, sizeof(temp), "JID %s terminated\n", num);
            write(STDOUT_FILENO, temp, strlen(temp));
            return;
        }
        current = current->next;
    }

    snprintf(temp, sizeof(temp), "JID %s terminated\n", num);
    write(STDOUT_FILENO, temp, strlen(temp));
}

/* Represents the jobs command */
void jobsCommand() {
    // remove old jobs
    struct ProcInfo * curr = head;
    while (curr != NULL) {
        int status;
        if (waitpid(curr->pid, &status, WNOHANG) > 0) {
            if (WIFEXITED(status)) removeBgProcess(curr->pid);
        }
        curr = curr ->next;
    }

    // print the currently running background jobs!
    curr = head;
    while (curr != NULL) {
        char temp[1000];
        snprintf(temp, sizeof(temp), "%d : %s\n", curr->jid, curr->line);
        write(STDOUT_FILENO, temp, strlen(temp));
        curr = curr->next;
    }
}

/* Returns 1 if redirection is specified, or 0 if redirection isn't specified
 * or -1 if an error occurs */
int isRedirection(char *cmds[], int len) {
    int countRedirection = 0;
    for (int i = 0; i < len; i++) {
         if (strcmp(cmds[i], ">") == 0) {
            countRedirection++;
            if (countRedirection > 1) {
                write(STDERR_FILENO, "Redirect Error: More than 1 >\n", 30);
                return -1;
            }

            if (len - i > 2) {
                write(STDERR_FILENO, "Redirect Error: args > 1 after >\n", 33);
                return -1;
            }

            cmds[i] = NULL;
            if (i - 1 < 0 || i + 1 >= len) {
                char * msg = "Redirect Error: No file or command\n";
                write(STDERR_FILENO, msg, strlen(msg));
                return -1;
            }
        }
    }

    return countRedirection == 0 ? 0 : 1;
}

/* Executes a given command */
void executeCommand(char *cmds[], int len, struct ProcInfo * cur) {
    // special commands such as exit, redirection, jobs, wait
    if (strcmp(cmds[0], "exit") == 0 && len == 1) {
        if (isBatchMode == 1) fclose(fileInput);
        freeResources();
        exit(0);
    } else if (strcmp(cmds[0], "wait") == 0 && len >= 2) {
        waitCommand(cmds[1]);
        return;
    } else if (strcmp(cmds[0], "jobs") == 0) {
        jobsCommand();
        return;
    }

    jobId++;
    if (cur != NULL) cur->jid = jobId;

    int processId = fork();
    if (processId < 0) {
        // fork failure
        write(STDERR_FILENO, "fork failed\n", 12);
        freeResources();
        if (isBatchMode == 1) fclose(fileInput);
        exit(1);
    } else if (processId == 0) {
        // if redirection occurs in the command
        int redirectionVal = isRedirection(cmds, len);
        if (redirectionVal == -1) {
            if (isBatchMode == 1) fclose(fileInput);
            freeResources();
            exit(1);
        } else if (redirectionVal == 1) {
            close(STDOUT_FILENO);
            int fd = open(cmds[len-1], O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
            if (fd == -1) {
                if (isBatchMode == 1) fclose(fileInput);
                freeResources();
                exit(0);
            }
        }

        // child process
        execvp(cmds[0], cmds);

        // if exec returned unsuccessfully
        write(STDERR_FILENO, cmds[0], strlen(cmds[0]));
        write(STDERR_FILENO, ": Command not found\n", 20);
        if (isBatchMode == 1) fclose(fileInput);
        freeResources();
        exit(0);
    } else {
        if (cur != NULL) {
            cur->pid = processId;
            return;
        }

        // parent process
        int status;
        waitpid(processId, &status, WCONTINUED | WUNTRACED);
    }
}

/* Main function - entry point of shell */
int main(int argc, char * argv[]) {
    // an input command  that can be a maximum of 512 characters long
    char line[MAX_LEN];

    // a single token to be read from the line declared above
    char * token;

    // the current index of a given command
    int cmdNum;

    fileInput = stdin;

    // given an invalid number of arguments
    if (argc > 2) {
        write(STDERR_FILENO, "Usage: mysh [batchFile]\n", 24);
        exit(1);
     } else if (argc == 2) {
        // BATCH MODE
        fileInput = fopen(argv[1], "r");
        if (fileInput == NULL) {
            write(STDERR_FILENO, "Error: Cannot open file ", 24);
            write(STDERR_FILENO, argv[1], strlen(argv[1]));
            write(STDERR_FILENO, "\n", strlen("\n"));
            exit(1);
        }

        isBatchMode = 1;
    }

     // INTERACTIVE MODE
    while (1) {
        if (isBatchMode == 0) {
            write(STDOUT_FILENO, "mysh> ", 6);
        }

        if (fgets(line, MAX_LEN, fileInput) == NULL) break;

        if (isBatchMode == 1) {
            write(STDOUT_FILENO, line, strlen(line));
        }

        // store the tokens in this array
        char * buffer[MAX_LEN / 2];
        cmdNum = 0;

        // tokenize the line and add to an array of commands
        token = strtok(line, " \n\t");
        while (token != NULL) {
            buffer[cmdNum] = token;
            token = strtok(NULL, " \n\t");
            cmdNum++;
        }
        buffer[cmdNum] = NULL;

        // if the user entered at least one command
        if (cmdNum != 0) {
            // given a process in the background
            struct ProcInfo * current = NULL;
            if (strcmp(buffer[0], "&") == 0) {
                write(STDOUT_FILENO, "exit\n", 5);
                freeResources();
                exit(0);
             } else if (cmdNum > 1 && strcmp(buffer[0], "&") != 0
                            && strcmp(buffer[cmdNum - 1], "&") == 0
                            && strcmp(buffer[0], "jobs")  != 0
                            && strcmp(buffer[0], "wait") != 0) {
                current = (struct ProcInfo *) malloc(sizeof(struct ProcInfo));
                if (current == NULL) {
                    freeResources();
                    exit(1);
                }
                current->next = NULL;
                buffer[cmdNum - 1] = NULL;
                strcpy(current->line, buffer[0]);
                for (int i = 1; i < cmdNum - 1; i++) {
                    strcat(current->line, " ");
                    strcat(current->line, buffer[i]);
                }
                addBgProcess(current);
                cmdNum -= 1;
            }

            executeCommand(buffer, cmdNum, current);
        }
    }

    if (isBatchMode == 1) fclose(fileInput);

    // remove dynamically allocated memory
    freeResources();
    return 0;
}
