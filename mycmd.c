#include <stdio.h>   // For printf, fprintf, fflush, fopen, perror, snprintf, fscanf, fclose, fgets
#include <stdlib.h>  // For exit, EXIT_FAILURE
#include <string.h>  // For memset, strcmp
#include <unistd.h>  // For fork, execvp
#include <sys/stat.h>  // For struct stat
#include <signal.h>   // For struct sigaction, sigaction
#include <sys/time.h> // For struct itimerval, setitimer
#include <dirent.h>   // For DIR, struct dirent, opendir, closedir
#include <sys/sysinfo.h> // For struct sysinfo, sysinfo
#include <pwd.h>      // For struct passwd, getpwuid
#include <sys/wait.h> // For waitpid, WIFEXITED, WEXITSTATUS
#include <time.h>
/////////////////////////////////////////////////////////////////////////


void executeCommand(char *cmd, char *args[]);
void executeCommandWithRedirect(char *cmd, char *args[], char *outputFile);
void displaySystemInfo();
void handle_refresh(int signum);
void executeCommandWithGrep(char *text, char *inputFile);
volatile sig_atomic_t refresh_flag = 0;
//-------------------------------------------------------
//=----------------------------------------------------------------------

int main(int argc, char *argv[]) {
    if (argc > 1) {
        // Check for the redirection operator
        int redirectIndex = -1;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], ">") == 0) {
                redirectIndex = i;
                break;
            }
        }

         if (redirectIndex != -1) {
            // Redirect output to a file
            if (redirectIndex + 1 < argc) {
                char *outputFile = argv[redirectIndex + 1];
                FILE *outputFilePtr = fopen(outputFile, "w");

                if (outputFilePtr == NULL) {
                    perror("Error opening output file");
                    exit(EXIT_FAILURE);
                }

                // Redirect stdout to the file
                if (dup2(fileno(outputFilePtr), STDOUT_FILENO) == -1) {
                    perror("Error redirecting stdout");
                    exit(EXIT_FAILURE);
                }

                fclose(outputFilePtr);

                // Remove the redirection-related arguments from the command
                for (int i = redirectIndex; i < argc - 2; i++) {
                    argv[i] = argv[i + 2];
                }
                argc -= 2;
            } else {
                fprintf(stderr, "Error: Missing output file after '>'\n");
                exit(EXIT_FAILURE);
            }
        }
 
        if (strcmp(argv[1], "top") == 0) {
            // Handle the "top" command
            // ... (rest of the "top" command handling)
            // Set up a timer to trigger the refresh every 10 seconds
            struct sigaction sa;
            struct itimerval timer;

            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = &handle_refresh;
            sigaction(SIGALRM, &sa, NULL);

            timer.it_value.tv_sec = 0;  // Set the initial timer to 0 for immediate execution
            timer.it_value.tv_usec = 0;
            timer.it_interval.tv_sec = 10;
            timer.it_interval.tv_usec = 0;
            setitimer(ITIMER_REAL, &timer, NULL);

            // Display system information immediately
            displaySystemInfo();

            char input;
            do {
                // Wait for 'q' to quit or timer signal to refresh
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(STDIN_FILENO, &readfds);

                struct timeval timeout;
                timeout.tv_sec = 10;  // Set the timeout to 10 seconds
                timeout.tv_usec = 0;

                printf("Press 'q' to quit: ");  // Move the print statement outside of the input check block
                fflush(stdout);
                int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);

                if (result == -1) {
                    perror("select");
                    exit(EXIT_FAILURE);
                } else if (result > 0) {
                    if (FD_ISSET(STDIN_FILENO, &readfds)) {
                        input = getchar();

                        if (input != 'q') {
                            // Consume extra characters (e.g., newline) from the input buffer
                            while ((getchar()) != '\n');

                            // Allow the user to input a command and its arguments
                            char cmd[256];
                            printf("Enter command: ");
                            scanf("%s", cmd);

                            char *args[argc];
                            args[0] = cmd;

                            for (int i = 1; i < argc; i++) {
                                scanf("%s", args[i]);
                            }
                            args[argc] = NULL;

                            // Execute the command
                            executeCommand(cmd, args);
                        }
                    }
                } else if (result == 0) {
                    // Timeout occurred, refresh the display
                    displaySystemInfo();
                }

            } while (input != 'q');
        }else if (redirectIndex != -1) {
            // Handle the command with input redirection
            if (redirectIndex + 2 < argc) {
                char *text = argv[redirectIndex + 1];
                char *inputFile = argv[redirectIndex + 2];
                executeCommandWithGrep(text, inputFile);
            } else {
                fprintf(stderr, "Usage: %s grep TEXT < input_file\n", argv[0]);
                exit(EXIT_FAILURE);
            }
        } else {
            // If there are arguments, execute the command directly
            executeCommand(argv[1], argv + 1);
        }
    } else {
        fprintf(stderr, "Usage: %s top or %s cmd arg1 ... argn > file\n", argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }

    return 0;
}
//------------------------------------------------------
void displaySystemInfo() {
    static int refresh_count = 0;  // Static variable to keep track of refresh count

    FILE *loadavg_file = fopen("/proc/loadavg", "r");
    if (loadavg_file == NULL) {
        perror("Error while opening /proc/loadavg");
        return;
    }

    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        // Display refresh count
        printf("System Information (Refresh #%d):\n", ++refresh_count);

        // Display load averages and total processes
        printf("| Load Average (1min)  | %-10.2f |\n", (double)info.loads[0] / (1 << SI_LOAD_SHIFT));
        printf("| Load Average (5min)  | %-10.2f |\n", (double)info.loads[1] / (1 << SI_LOAD_SHIFT));
        printf("| Load Average (15min) | %-10.2f |\n", (double)info.loads[2] / (1 << SI_LOAD_SHIFT));
        printf("| Total Processes      | %-10lu |\n", (unsigned long)info.procs);

        // Display each process without columns
        printf("\nProcesses (Running):\n");
        unsigned long procs_running = 0;
        unsigned long max_procs = 20;  // Limit the display to the first 20 processes

        DIR *dir = opendir("/proc");
        if (dir != NULL) {
            struct dirent *entry;

            while ((entry = readdir(dir)) != NULL && procs_running < max_procs) {
                if (entry->d_type == DT_DIR && atoi(entry->d_name) != 0) {
                    char path[512];
                    snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);

                    struct stat status;
                    if (stat(path, &status) == 0) {
                        char status_char = 'Z';
                        if (status.st_mode & S_IFDIR) {
                            FILE *stat_file = fopen(path, "r");
                            if (stat_file != NULL) {
                                char stat_char;
                                if (fscanf(stat_file, "%*d %*s %c", &stat_char) == 1 && stat_char == 'R') {
                                    status_char = 'R';
                                    procs_running++;
                                }
                                fclose(stat_file);
                            }
                        }

                        uid_t uid = status.st_uid;
                        struct passwd *pwd = getpwuid(uid);

                        // Display information about each process
                        printf("| PID: %-6s | Status: %-6c | UID: %-8d | ", entry->d_name, status_char, uid);

                        // command line access
                        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
                        FILE *cmdline_file = fopen(path, "r");
                        if (cmdline_file != NULL) {
                            char cmdline[512];  // Increased buffer size
                            fgets(cmdline, sizeof(cmdline), cmdline_file);
                            printf("Command: %-20s | ", cmdline);
                            fclose(cmdline_file);
                        } else {
                            printf("Command: %-20s | ", "N/A");
                        }

                        // getting username
                        if (pwd != NULL) {
                            printf("Username: %-12s |\n", pwd->pw_name);
                        } else {
                            printf("Username: %-12s |\n", "N/A");
                        }
                    }
                procs_running++;
                }
            }

            closedir(dir);
        }

        printf("\n");
    } else {
        perror("Error while retrieving system information");
    }

    fclose(loadavg_file);
}

//-------------------------------------------------------
/*void displaySystemInfo() {
    FILE *loadavg_file = fopen("/proc/loadavg", "r");
    if (loadavg_file == NULL) {
        perror("Error while opening /proc/loadavg");
        return;
    }

    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        printf("System Information:\n");
        printf("| %-6s | %-6s | %-8s | %-20s | %-12s |\n", "PID", "Status", "UID", "Command", "Username");
        printf("|--------|--------|----------|----------------------|--------------|\n");
        printf("| Load Average (1min)  | %-10.2f |\n", (double)info.loads[0] / (1 << SI_LOAD_SHIFT));
        printf("| Load Average (5min)  | %-10.2f |\n", (double)info.loads[1] / (1 << SI_LOAD_SHIFT));
        printf("| Load Average (15min) | %-10.2f |\n", (double)info.loads[2] / (1 << SI_LOAD_SHIFT));
        printf("| Total Processes      | %-10lu |\n", (unsigned long)info.procs);
        printf("\n| PID    | Status | UID      | Command              | Username     |\n");
        printf("|--------|--------|----------|----------------------|--------------|\n");

        // Count processes in the running state
        unsigned long procs_running = 0;
        unsigned long max_procs = 20;  // Limit the display to the first 20 processes

        DIR *dir = opendir("/proc");
        if (dir != NULL) {
            struct dirent *entry;

            while ((entry = readdir(dir)) != NULL && procs_running < max_procs) {
                if (entry->d_type == DT_DIR && atoi(entry->d_name) != 0) {
                    char path[512];
                    snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);

                    struct stat status;
                    if (stat(path, &status) == 0) {
                        char status_char = 'Z';
                        if (status.st_mode & S_IFDIR) {
                            FILE *stat_file = fopen(path, "r");
                            if (stat_file != NULL) {
                                char stat_char;
                                if (fscanf(stat_file, "%*d %*s %c", &stat_char) == 1 && stat_char == 'R') {
                                    status_char = 'R';
                                    procs_running++;
                                }
                                fclose(stat_file);
                            }
                        }

                        uid_t uid = status.st_uid;
                        struct passwd *pwd = getpwuid(uid);

                        printf("| %-6s | %-6c | %-8d | ", entry->d_name, status_char, uid);

                        // command line access
                        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
                        FILE *cmdline_file = fopen(path, "r");
                        if (cmdline_file != NULL) {
                            char cmdline[512];  // Increased buffer size
                            fgets(cmdline, sizeof(cmdline), cmdline_file);
                            printf("%-20s | ", cmdline);
                            fclose(cmdline_file);
                        } else {
                            printf("%-20s | ", "N/A");
                        }

                        // getting username
                        if (pwd != NULL) {
                            printf("%-12s |\n", pwd->pw_name);
                        } else {
                            printf("%-12s |\n", "N/A");
                        }
                    }
                procs_running++;
                }
            }

            closedir(dir);
        }

        printf("| Processes (Running) | %-10lu |\n", procs_running);
    } else {
        perror("Error while retrieving system information");
    }

    fclose(loadavg_file);
    printf("\n");
}*/
//-------------------------------------------------------
// Function to handle the execution of Linux commands
void executeCommand(char *cmd, char *args[]) {
    if (strcmp(cmd, "top") == 0) {
        displaySystemInfo();
    } else {
        pid_t pid;
        int status;

        pid = fork();

        if (pid == -1) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {  // Child process
            if (execvp(cmd, args) == -1) {
                perror("Execution failed");
                exit(EXIT_FAILURE);
            }
        } else {  // Parent process
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                fprintf(stderr, "Command execution failed with status %d\n", WEXITSTATUS(status));
                exit(EXIT_FAILURE);
            }
        }
    }
}
//---------------------------------------------------------------------------------
void executeCommandWithRedirect(char *cmd, char *args[], char *outputFile) {
    pid_t pid;
    int status;

    pid = fork();

    if (pid == -1) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {  // Child process
        // Redirect standard output to the specified file
        freopen(outputFile, "w", stdout);

        if (args == NULL) {
            if (execlp(cmd, cmd, (char *)NULL) == -1) {
                perror("Execution failed");
                exit(EXIT_FAILURE);
            }
        } else {
            if (execvp(cmd, args) == -1) {
                perror("Execution failed");
                exit(EXIT_FAILURE);
            }
        }
    } else {  // Parent process
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Command execution failed with status %d\n", WEXITSTATUS(status));
            exit(EXIT_FAILURE);
        }
    }
}
//-----------------------------------------------------------------------
void handle_refresh(int signum) {
    refresh_flag = 1;
}
//-----------------------------------------------------
void executeCommandWithInputRedirect(char *cmd, char *args[], char *inputFile) {
    pid_t pid;
    int status;

    pid = fork();

    if (pid == -1) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {  // Child process
        // Redirect standard input from the specified file
        freopen(inputFile, "r", stdin);

        if (execvp(cmd, args) == -1) {
            perror("Execution failed");
            exit(EXIT_FAILURE);
        }
    } else {  // Parent process
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Command execution failed with status %d\n", WEXITSTATUS(status));
            exit(EXIT_FAILURE);
        }
    }
}
//------------------------------------------------------
void executeCommandWithGrep(char *text, char *inputFile) {
    // Create a pipe for communication between processes
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {  // Child process
        // Close the write end of the pipe since we are only reading from it
        close(pipefd[1]);

        // Redirect standard input from the specified file
        freopen(inputFile, "r", stdin);

        // Redirect standard output to the pipe
        dup2(pipefd[0], STDIN_FILENO);

        // Execute the "grep" command
        execlp("grep", "grep", text, (char *)NULL);

        // If execlp fails
        perror("Execution failed");
        exit(EXIT_FAILURE);
    } else {  // Parent process
        // Close the read end of the pipe since we are only writing to it
        close(pipefd[0]);

        // Wait for the child process to finish
        waitpid(pid, NULL, 0);

        // Close the write end of the pipe to signal the end of writing
        close(pipefd[1]);
    }
}

//-------------------------------------------------------
/*void displaySystemInfo() {
    FILE *loadavg_file = fopen("/proc/loadavg", "r");
    if (loadavg_file == NULL) {
        perror("Error while opening /proc/loadavg");
        return;
    }

    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        printf("System Information:\n");
        printf("| %-20s | %-10s |\n", "Parameter", "Value");
        printf("|----------------------|------------|\n");
        printf("| Load Average (1min)  | %-10.2f |\n", (double)info.loads[0] / (1 << SI_LOAD_SHIFT));
        printf("| Load Average (5min)  | %-10.2f |\n", (double)info.loads[1] / (1 << SI_LOAD_SHIFT));
        printf("| Load Average (15min) | %-10.2f |\n", (double)info.loads[2] / (1 << SI_LOAD_SHIFT));
        printf("| Total Processes      | %-10lu |\n", (unsigned long)info.procs);

        // Count processes in the running state
        unsigned long procs_running = 0;
        unsigned long max_procs = 20;  // Limit the display to the first 20 processes

        DIR *dir = opendir("/proc");
        if (dir != NULL) {
            struct dirent *entry;

            while ((entry = readdir(dir)) != NULL && procs_running < max_procs) {
                if (entry->d_type == DT_DIR && atoi(entry->d_name) != 0) {
                    char path[512];
                    snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);

                    struct stat status;
                    if (stat(path, &status) == 0) {
                        char status_char = 'Z';
                        if (status.st_mode & S_IFDIR) {
                            FILE *stat_file = fopen(path, "r");
                            if (stat_file != NULL) {
                                char stat_char;
                                if (fscanf(stat_file, "%*d %*s %c", &stat_char) == 1 && stat_char == 'R') {
                                    status_char = 'R';
                                    procs_running++;
                                }
                                fclose(stat_file);
                            }
                        }

                        uid_t uid = status.st_uid;
                        struct passwd *pwd = getpwuid(uid);

                        printf("| %-6s | %-6c | %-8d | ", entry->d_name, status_char, uid);

                        // command line access
                        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
                        FILE *cmdline_file = fopen(path, "r");
                        if (cmdline_file != NULL) {
                            char cmdline[512];  // Increased buffer size
                            fgets(cmdline, sizeof(cmdline), cmdline_file);
                            printf("%-20s | ", cmdline);
                            fclose(cmdline_file);
                        } else {
                            printf("%-20s | ", "N/A");
                        }

                        // getting username
                        if (pwd != NULL) {
                            printf("%-12s |\n", pwd->pw_name);
                        } else {
                            printf("%-12s |\n", "N/A");
                        }
                    }
                procs_running++;
                }
            }

            closedir(dir);
        }

        printf("| Processes (Running) | %-10lu |\n", procs_running);
    } else {
        perror("Error while retrieving system information");
    }

    fclose(loadavg_file);
    printf("\n");
}*/

//-----------------------------------------------------------------------
