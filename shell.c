#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <time.h>
#include <sys/wait.h>

#define BUFFER_SIZE 64
#define TOKENIZING_CHARS " \n"

int backgroundFail;
int stall = 0;
int toggleBg = 0;
int lastStatus = -10000;
int exitStatus = 0;
int backgroundEnabled = 1;
int backgroundProcess[100];
char* backgroundArg[100];
int redirectIO;


char* getCmdLineInput() {
    printf(": ");

    //Get the user input from command line
    char *cmdLine = NULL;
    ssize_t size = 0;
    getline(&cmdLine, &size, stdin);

    return cmdLine;
}

char** splitInput(char* input) {
    int bufferSize = BUFFER_SIZE;
    char **args = malloc(bufferSize * sizeof(char *));
    char *token;

    int position = 0;

    token = strtok(input, TOKENIZING_CHARS);

    while (token != NULL) {
        args[position] = token;
        position++;

        if (position >= bufferSize) {
            bufferSize += BUFFER_SIZE;
            args = realloc(args, bufferSize * sizeof(char *));
        }

        token = strtok(NULL, TOKENIZING_CHARS);
    }

    args[position] = NULL;

    return args;
}

void setTerminate(int signal) {
    kill(getpid(), signal);
}

int executeCmd(char** args) {
    backgroundFail = 0;
    stall = 0;

    //blank line OR comments check
    if (args[0] == NULL || args[0][0] == '#') { return 1; }

    if (strcmp(args[0], "exit") == 0) { return 0; }

    if (strcmp(args[0], "status") == 0) {
        // printExitStatus(exitStatus);
        if (exitStatus) {
          if (!lastStatus) { printf("exit value is %d\n",lastStatus); } // successful (0) exit status
          else { printf("exit value is 1\n"); } // non-zero exit status
        }
        else { printf("terminated by signal %d\n",lastStatus); }

        return(1);
    }

    if (strcmp(args[0], "cd") == 0) {
        //response if no args
        if (args[1] == NULL) { chdir(getenv("HOME")); }
        //repsonse if args
        else {
            if (chdir(args[1]) != 0) {}
        }

        return 1;
    }

    int position = 0;
    int background = 0;
    while (args[position] != NULL) {
        if (strcmp(args[position], "$$") == 0) {
            int pid = getpid();
            char pidStr[10];
            sprintf(pidStr, "%d", pid);

            args[position] = NULL;
            args[position] = pidStr;
        }

        position++;
    }

    position = 0;
    while (args[position] != NULL) {
        if ((strcmp(args[position], "&") == 0) && (args[position+1] == NULL)) {
            if (backgroundEnabled) {
                background = 1;
            }

            args[position] = NULL;
        }

        position++;
    }

    int check;
    int swap;

    pid_t pid = fork(); //create the child process
    if ((pid != 0) && (background == 1)) {
        printf("background pid is %d\n", pid);

        int finished = 0;
        for (int i = 0; i < 100; i++) {
            if ((backgroundProcess[i] == -1) && (!finished)) {
                backgroundProcess[i] = pid;
                backgroundArg[i] = args[0];
                finished = 1;
            }
            else if (finished) { break; }
        }
    }

    if (pid == 0) { //Child process executes cmd
        signal(SIGTSTP, NULL); //ignores ctrl+z
        if (!background) { signal(SIGINT, setTerminate); } //catch ctrl+c if child in foreground
        if (background == 1) {
            int defaultIn = open("/dev/null", O_RDONLY);
            int defaultOut = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            swap = dup2(defaultIn, 0);
            swap = dup2(defaultOut, 1);
        }

        redirectIO = -1;
        position = 0;
        while (args[position] != NULL) {

            if (strcmp(args[position], "<") == 0) {
                if (redirectIO == -1) { redirectIO = position; }

                int newInput = open(args[position+1], O_RDONLY);
                if (newInput == -1) {
                    printf("cannot open %s for input file\n", args[position+1]);
                    exit(1);
                }
                else { swap = dup2(newInput, 0); }
            }

            if (strcmp(args[position], ">") == 0) {
                if (redirectIO == -1) { redirectIO = position; }

                int newOutput = open(args[position+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                swap = dup2(newOutput, 1);
            }

            position++;
        }

        if (redirectIO != -1) { args[redirectIO] = NULL; }
        if (execvp(*args, args) != 0) {
            if (!background) {
                perror(args[0]);
                exit(1);
            }
        }
        else {
            if (!background) { exit(0); }
        }
    }
    else { //Parent process executes cmd
        if (background == 0) { //checking if foreground process
            pid_t wait = waitpid(pid, &check, WUNTRACED); //waits for child to finish process

            if ((check == 2) || (check == 15)) { //manually terminated?
                printf("terminated by signal %d\n", check);
                exitStatus = 0;
            }
            else { exitStatus = 1; }

            lastStatus = check;
        }
    }

    return 1;
}




/*
void printExitStatus(int status) {
    if (WIFEXITED(status)) { printf("exit value %d\n", WEXITSTATUS(status)); }
    else { printf("terminated by signal %d\n", WTERMSIG(status)); } //not as sure about this, could just be "status" instead of WTERMSIG
}
*/

void killChildProcesses() {
    for (int i = 0; i < 100; i++) {
        if (backgroundProcess[i] != -1) {
            kill(backgroundProcess[i], 2092);
            backgroundProcess[i] = -1;
        }
    }
}

void backgroundToggle() {
    if (toggleBg) {
        if(backgroundEnabled) {
            printf("foreground only! (the & will be ignored)\n");
            backgroundEnabled = 0;
        }
        else {
            printf("no longer foreground only! (the & will NOT be ignored)\n");
            backgroundEnabled = 1;
        }

        toggleBg = 0; //flip switch from bg mode
    }
}

void toggleBackground() { //enter background mode
    toggleBg = 1; //flip switch
}

void shellProcess() {
    char* cmdLineInput;
    char** args;
    int shellStatus = 1;

    while (shellStatus) { //loops while status is active
        //check for background processes running

        //check for toggling background

        //get cmd line input from user
        cmdLineInput = getCmdLineInput();
        args = splitInput(cmdLineInput);
        shellStatus = executeCmd(args);

        fflush(stdout); //flush text output
        free(cmdLineInput); //free input
        free(args); //free split input / arguments
    }

    killChildProcesses();
}

int main() {
    signal(SIGTSTP, toggleBackground); //catch ctrl+z in parent
    struct sigaction ignoreSig;
    sigfillset(&ignoreSig.sa_mask);
    ignoreSig.sa_handler = SIG_IGN;
    ignoreSig.sa_flags = 0;
    sigaction(SIGINT, &ignoreSig, NULL);

    for (int i = 0; i < 100; i++) {
        backgroundProcess[i] = -1;
        backgroundArg[i] = NULL;
    }

    shellProcess();
}
