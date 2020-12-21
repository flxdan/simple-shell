/*
 * Program:  Smallsh 
 * Author: Daniel Felix
 * --------------------
 * A simple shell program to run command line commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <sys/wait.h>

// Array to keep track of processes in the background
int processesArray[512];

// Number of child processes
int currChildren = 0;

// The PID of the most current foreground
int foregroundPID = -33;

// Bool to keep track of foreground only mode triggered
bool foregroundOnly = false;

// Bool to continue program exection
bool running = true;

// Status value for more recent foreground process
int childExitMethod;



/*
 * Struct:  commandStruct 
 * --------------------
 * A struct to hold command prompt input 
 */
struct commandStruct {
    char *command;
    char *arguments[512];
    int totalArgu;
    char *inFile;
    char *outFile;
    bool background;
};

/*
 * Function:  isComment 
 * --------------------
 * Check if an input is a comment
 */
int isComment(struct commandStruct *cmd) {
    if (strncmp(cmd->arguments[0], "#", 1) == 0) {
        return 1;
    } else {
        return 0;
    }
}

/*
 * Function:  printCMD
 * --------------------
 * Print the command data given at input
 */
void printCMD(struct commandStruct *cmd) {
    printf("Command: %s\n", cmd->command);
    for (int i = 0; i < cmd->totalArgu; i++) {
        printf("%s\n", cmd->arguments[i]);
        fflush(stdout);
    }
    printf("Number of arguments: %d\n", cmd->totalArgu);
    fflush(stdout);
    printf("inFile: %s\n", cmd->inFile);
    fflush(stdout);
    printf("outFile: %s\n", cmd->outFile);
    fflush(stdout);
    printf("Background mode?: %d\n", cmd->background);
    fflush(stdout);
}

/*
 * Function:  checkBackgrounded 
 * --------------------
 * Check if the last character in the given input is a &
 * If yes, set the background value in the struct to true, else false
 */
void checkBackground(struct commandStruct *cmd) {
    if (cmd->totalArgu < 2) return;
    if (foregroundOnly == false) {}

    // If the last character is &
    if (strcmp(cmd->arguments[cmd->totalArgu-1], "&") == 0){
        // If we are NOT in foreground only mode 
        if (foregroundOnly == false) {
            cmd->background = true;
            // Remove from command struct arguements array and decrease arguement count
            cmd->arguments[cmd->totalArgu-1] = NULL;
            cmd->totalArgu--;
        // If we are in foreground only mode ignore &
        } else {
            cmd->arguments[cmd->totalArgu-1] = NULL;
            cmd->totalArgu--;
        }
    }
}

/*
 * Function:  checkDirection 
 * --------------------
 * Check if an input or output file is given. If so update command struct
 */
void checkDirection(struct commandStruct *cmd) {
    // Check that atleast two arguments were given 
    if (cmd->totalArgu < 2) return;
    // Check if the second to last character is < or >
    if (strcmp(cmd->arguments[cmd->totalArgu-2], "<") == 0 || strcmp(cmd->arguments[cmd->totalArgu-2], ">") == 0) {
        // If character is > update command struct data for output file
        if (strcmp(cmd->arguments[cmd->totalArgu-2], ">") == 0) {
            cmd->outFile = calloc(strlen(cmd->arguments[cmd->totalArgu-1]), sizeof(char));
            strcpy(cmd->outFile, cmd->arguments[cmd->totalArgu-1]);
            // Update command struct
            cmd->arguments[cmd->totalArgu-1] = NULL;
            cmd->arguments[cmd->totalArgu-2] = NULL;
            cmd->totalArgu = cmd->totalArgu - 2;
        // If chacter is < update command struct data for input file
        } else if (strcmp(cmd->arguments[cmd->totalArgu-2], "<") == 0){
            cmd->inFile = calloc(strlen(cmd->arguments[cmd->totalArgu-1]), sizeof(char));
            strcpy(cmd->inFile, cmd->arguments[cmd->totalArgu-1]);
            // Update command struct
            cmd->arguments[cmd->totalArgu-1] = NULL;
            cmd->arguments[cmd->totalArgu-2] = NULL;
            cmd->totalArgu = cmd->totalArgu - 2;
        }
    } 
}

/*
 * Function:  tokenize 
 * --------------------
 * Check if an input is a comment
 */
void tokenize(struct commandStruct *cmd, char *input) {
    // Strtok to seperate by space or end of line
    char *command = strtok(input, " \n");

    // Tokenize input and put each token inside of the arugments array
    while (command != NULL) {
        // Create space in array and update command struct
        cmd->arguments[cmd->totalArgu] = calloc(strlen(command), sizeof(char));
        strcpy(cmd->arguments[cmd->totalArgu], command);
        // Get next token
        command = strtok(NULL, " \n"); 
        cmd->totalArgu++;
    }
}

/*
 * Function:  getCommand 
 * --------------------
 * Extract the command from arugments list
 */
void getCommand(struct commandStruct *cmd) {
    // Check if a comamnd was given
    if (cmd->totalArgu < 1) return;
    cmd->command = calloc(strlen(cmd->arguments[0]), sizeof(char));
    strcpy(cmd->command, cmd->arguments[0]);
    
    // Make sure last element in array is NULL to later use for my exec function
    cmd->arguments[cmd->totalArgu] = NULL;
}

/*
 * Function:  convertPid 
 * --------------------
 * Replace all instances of $$ with the parent PID
 */
char *convertPID(char *str) {
    char *needle;
    // Get the pid
    pid_t cur_pid = getpid();
    char pid_string[25];
    sprintf(pid_string, "%d", cur_pid);

    // Check if $$ was found inside of the string 
    if ((needle = strstr(str, "$$")) != NULL)  {
        // Create new return string
        char* return_str = calloc(strlen(str) + 1, sizeof(char));
        // $$ was found at the start of the string
        if (strlen(needle) == strlen(str)) {
            strcpy(return_str, pid_string);
        // $$ was found inside of string 
        } else {
            strncpy(return_str, str, needle - str);
            strcat(return_str, pid_string);
        }
        // Concatanate the rest of the string
        strcat(return_str, needle + 2);

        // Recursively check if the string contains more instances of $$
        return convertPID(return_str);
    }
    // Base case: No instances of $$ were found
    return str;
}

/*
 * Function:  exitSH 
 * --------------------
 * Kill all backgrounded processes if exit was given as command
 */
void exitSH() {
    for (int i = 0; i < currChildren; i++) {
        kill(processesArray[i], SIGKILL);
    }
}

/*
 * Function:  changeCD 
 * --------------------
 * Change the current directory
 */
void changeCD(struct commandStruct *cmd) {
    if (cmd->totalArgu > 1) {
        // Change directory to directory given
        chdir(cmd->arguments[1]);
    } else {
        // Change directory to the home directory
        chdir(getenv("HOME"));
    }
}

/*
 * Function:  getStatus 
 * --------------------
 * get the status of the most recent foreground process
 */
void getStatus(int status) {
    // Check if one of the non-build in functions was called
    if (foregroundPID != -33) {
        // Return exit value
        if(WIFEXITED(status)){
            printf("exit value %d\n", WEXITSTATUS(status));
        } else {
            // Return termination signal
            printf("terminated by signal %d\n", WTERMSIG(status));
        }
    } else {
        printf("exit value 0\n");
    }
}

/*
 * Function:  addToArray 
 * --------------------
 * Add backgrounded process to array
 */
void addToArray(int pid) {
    processesArray[currChildren] = pid;
    currChildren++;
}

/*
 * Function:  changeBackgrounded 
 * --------------------
 * Change background bool value to toggle background mode on/off
 */
void changeBackgrounded(int signo) {
    // Make sure you are running the shell
    int currStatus;
    waitpid(foregroundPID, &currStatus, 0);
    
    if (foregroundOnly) {
        char *message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
    } else {
        char *message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
    }

    // Change background bool
    foregroundOnly = !foregroundOnly;
}

/*
 * Function:  removeProcess 
 * --------------------
 * Remove a process from the child processes array
 */
void removeProcess(int pid) {
    int index = 1000;

    // Iterate over array and find the index for the matching PID
    for (int i = 0; i < currChildren; i++) {
        if (processesArray[i] == pid) {
            index = i;
        }
    }

    // PID was found
    if (index != 1000) {
        currChildren--;

        // Move every element to the left
        for (int i = index; i < currChildren; i++) {
            processesArray[i] = processesArray[i+1];
        }
    }

}

/*
 * Function:  runCommand 
 * --------------------
 * Handle commnad execution
 */
void runCommand(struct commandStruct *cmd, struct sigaction SIGINT_action, struct sigaction SIGTSTP_action) {
    pid_t spawnPid = -5;

    // Check if the given command is one of the 3 built in commands
    if (strcmp(cmd->command, "exit") == 0) {
        exitSH();
        running = false;
    } else if (strcmp(cmd->command, "cd") == 0) {
        changeCD(cmd);
    } else if (strcmp(cmd->command, "status") == 0) {
        getStatus(childExitMethod);
    // Non built in command
    } else {
        // Fork child process
        spawnPid = fork();

        switch (spawnPid){
        case -1:
            perror("fork() failed\n");
            exit(1);
            break;
        // Case for children processes
        case 0:
            // All children ignore SIGTSTP
            SIGTSTP_action.sa_handler = SIG_IGN;
            SIGTSTP_action.sa_flags =  0;
            // Change signal handler to ignore SIGTSTP
            sigaction(SIGTSTP, &SIGTSTP_action, NULL);

            // If backgrounded set input and output files to /dev/null
            if (cmd->background) {
                if (cmd->inFile == NULL) {
                    int nullIn = open("/dev/null", O_RDONLY);
                    dup2(nullIn, 0);
                }
                if (cmd->outFile == NULL) {
                    int nullOut = open("/dev/null", O_WRONLY);
                    dup2(nullOut, 0);
                }
            // Not backgrouneded
            } else {
                // A child running as a foreground process must terminate itself when it receives SIGINT
                SIGINT_action.sa_handler = SIG_DFL;
                SIGINT_action.sa_flags = 0;
                // Change signal handler to default 
                sigaction(SIGINT, &SIGINT_action, NULL);
            }

            // Set input file 
            if (cmd->inFile != NULL) {
                // Open file 
                int sourceFD = open(cmd->inFile, O_RDONLY);
                if (sourceFD == -1) { 
                    fprintf(stderr, "cannot open %s for input\n", cmd->inFile);
                    fflush(stdout);
                    exit(1); 
                }
                // Redirect input
                int result = dup2(sourceFD, 0);
                if (result == -1) { 
                    perror("source dup2()"); 
                    exit(1); 
                }
            }

            // Set output file
            if (cmd->outFile != NULL) {
                // Open file
                int targetFD = open(cmd->outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (targetFD == -1) { 
                    perror("target open()"); 
                    exit(1); 
                }
                // Redirect output 
                int result = dup2(targetFD, 1);
                if (result == -1) { 
                    perror("target dup2()"); 
                    exit(1); 
                }
            }

            // Call execvp to run non-built in command. Cmd-> arguments is array with command and arguments
            execvp(cmd->command, cmd->arguments);

            perror(cmd->command);
            exit(1);
        // Parent process
        default:
            // Print out the pid of the backgrounded process
            if (cmd->background) {
                printf("background pid is %d\n", spawnPid);
                fflush(stdout);

                // Add pid to array of background processes
                addToArray(spawnPid);
            // The command is in the foreground
            } else {
                foregroundPID = spawnPid;
                // Wait for the foreground process to finish
                waitpid(spawnPid, &childExitMethod, 0);

                // Check if the foreground process was terminated by a signal
                if (WIFSIGNALED(childExitMethod)) {
                    printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
                    fflush(stdout);
                }

            }
            break;
        }
    }

    // Check backgrounded processes to see if any of the finished
    int backGroundStatus;
    pid_t checkProcess = waitpid(-1, &backGroundStatus, WNOHANG);

    if ((checkProcess > 0) && (running == true)) {
        // Remove from processes array
        removeProcess(checkProcess);

        // If exited, display pid and exit status 
        if (WIFEXITED(backGroundStatus)) {
            printf("background pid %d is done: exit value %d\n", checkProcess, WEXITSTATUS(backGroundStatus));
            fflush(stdout);
        // If terminated, display pid and termination signal
        } else {
            printf("background pid %d is done: terminated by signal %d\n", checkProcess, WTERMSIG(backGroundStatus));
            fflush(stdout);
        }
    }
    
}

/*
 * Function:  main 
 * --------------------
 * Main function to run program
 */
int main(void){
    

    // Set up SIG INT handler
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Set up SIGTSTP handler
    struct sigaction SIGTSTP_action = {0};
    memset(&SIGTSTP_action, 0, sizeof(SIGTSTP_action));
    SIGTSTP_action.sa_handler = changeBackgrounded;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Run program until exit 
    while (running){

        char* input = calloc(2050, sizeof(char));
        printf(": ");
        fflush(stdout);
        
        // Check if input was given. Prevents errors related to pressing Control-Z
        if (fgets(input, 2050, stdin) != NULL) {
            // Check max length
            if (strlen(input) > 2049) continue;
            
            // Check for blank line
            if (strlen(input) == 1) continue;


            // Create new string for input with && replaced with PID
            char* new_input = calloc(2050, sizeof(char));

            // Replace $$ with PID
            strcpy(new_input,convertPID(input));

            // Create command struct
            struct commandStruct *cmd = (struct commandStruct*)malloc(sizeof(struct commandStruct));

            // Tokenize input
            tokenize(cmd, new_input);
            
            // Check for comment
            if (isComment(cmd)) continue;

            // Check if background given
            checkBackground(cmd);

            // Check for first input and output file
            checkDirection(cmd);

            // Check for second input and output file
            checkDirection(cmd);

            // Get command
            getCommand(cmd);
            // Run the program 
            runCommand(cmd, SIGINT_action, SIGTSTP_action);

        }
    
    }
    
    return 0;
}
