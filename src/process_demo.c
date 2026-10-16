/*
 * process_demo.c — fork() -> exec() -> waitpid() demonstration.
 *
 * Every printed value is real: PIDs come from getpid()/fork(), the exit
 * report comes from waitpid(). The child replaces itself with /bin/echo
 * via execl(), demonstrating true program replacement.
 */
#include "process_demo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int run_process_demo(void)
{
    pid_t pid;
    int status = 0;
    pid_t parent_pid = getpid();

    printf("=================================================================\n");
    printf("              PROCESS CREATION DEMONSTRATION\n");
    printf("=================================================================\n");
    printf("\n");
    printf("Parent PID : %d\n", (int)parent_pid);
    printf("\n");
    printf("Calling fork()...\n");
    printf("\n");

    fflush(stdout); /* CRITICAL: flush so the child does not re-print buffered output */
    pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* ---- child process ---- */
        printf("Child PID  : %d (created by fork)\n", (int)getpid());
        printf("Child PPID : %d (the parent above)\n", (int)getppid());
        printf("\n");
        printf("Child calling exec() to replace itself with /bin/echo...\n");
        printf("\n");
        fflush(stdout);

        execl("/bin/echo", "echo",
              "Hello from the child process (via exec).", (char *)NULL);

        /* execl only returns on failure */
        perror("execl");
        _exit(127);
    }

    /* ---- parent process ---- */
    printf("fork() returned to the parent with the child's PID: %d\n",
           (int)pid);
    printf("\n");
    printf("The child's exec'd output appears above ^\n");
    printf("\n");
    printf("Parent calling waitpid() to synchronise with the child...\n");
    printf("\n");

    {
        pid_t w = waitpid(pid, &status, 0);
        if (w < 0) {
            perror("waitpid");
            return -1;
        }
        printf("waitpid() returned: %d (child PID)\n", (int)w);
        if (WIFEXITED(status)) {
            printf("Child exited normally.\n");
            printf("Exit status: %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Child was terminated by signal %d.\n",
                   WTERMSIG(status));
        } else {
            printf("Child ended in an unexpected way.\n");
        }
    }

    printf("\n");
    printf("Concept chain demonstrated:\n");
    printf("  fork() -> child process -> execl() -> program execution\n");
    printf("         -> waitpid() -> parent synchronisation\n");
    printf("\n");
    return 0;
}
