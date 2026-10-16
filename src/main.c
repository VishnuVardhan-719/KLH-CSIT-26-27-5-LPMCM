/*
 * main.c — entry point of the Linux Process Monitoring & Control System.
 *
 * Modes:
 *   (none)  interactive live monitor (raw-mode TUI, alternate screen)
 *   --once  print the process table once and exit (batch/evidence mode)
 *   --tree  print the process tree once and exit (batch/evidence mode)
 *   --demo  run the fork/exec/waitpid demonstration and exit
 *   --help  usage
 *
 * All process data is read live from /proc; nothing is hardcoded.
 */
#define _POSIX_C_SOURCE 200809L

#include "process_demo.h"
#include "process_monitor.h"
#include "system_info.h"
#include "terminal_ui.h"
#include "utils.h"

#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The locale charset decides whether the tree may draw Unicode branches. */
static int terminal_is_utf8(void)
{
    const char *cs = nl_langinfo(CODESET);
    return cs && (strstr(cs, "UTF-8") != NULL || strstr(cs, "utf8") != NULL);
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [OPTION]\n", prog);
    printf("\n");
    printf("Linux Process Monitoring and Control System (terminal edition)\n");
    printf("\n");
    printf("Options:\n");
    printf("  (none)    interactive live monitor (TUI)\n");
    printf("  --once    print the process table once and exit\n");
    printf("  --tree    print the process tree once and exit\n");
    printf("  --demo    run the fork/exec/waitpid demonstration and exit\n");
    printf("  --help    show this help and exit\n");
    printf("\n");
    printf("All process data is read live from the Linux /proc filesystem.\n");
}

static int do_batch_table(void)
{
    MonitorState *mon;
    ProcVec vec;
    SystemStats stats;
    int rc = 0;

    memset(&vec, 0, sizeof vec);
    memset(&stats, 0, sizeof stats);
    system_info_read_static(&stats); /* hostname, kernel, CPUs, MemTotal */

    mon = monitor_create();
    if (!mon) {
        fprintf(stderr, "error: cannot initialise the monitor\n");
        return 1;
    }
    if (monitor_refresh(mon, &vec, &stats) == (size_t)-1) {
        fprintf(stderr, "error: cannot read /proc (%s)\n", strerror(errno));
        rc = 1;
    } else {
        struct timespec delay = {0, 250 * 1000 * 1000};
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
            ;
        if (monitor_refresh(mon, &vec, &stats) == (size_t)-1) {
            fprintf(stderr, "error: cannot read /proc (%s)\n",
                    strerror(errno));
            rc = 1;
        } else {
            ui_print_batch_table(&vec, &stats);
        }
    }

    free(vec.items);
    monitor_destroy(mon);
    return rc;
}

static int do_batch_tree(void)
{
    MonitorState *mon;
    ProcVec vec;
    SystemStats stats;
    int rc = 0;

    memset(&vec, 0, sizeof vec);
    memset(&stats, 0, sizeof stats);
    system_info_read_static(&stats);

    mon = monitor_create();
    if (!mon) {
        fprintf(stderr, "error: cannot initialise the monitor\n");
        return 1;
    }
    if (monitor_refresh(mon, &vec, &stats) == (size_t)-1) {
        fprintf(stderr, "error: cannot read /proc (%s)\n", strerror(errno));
        rc = 1;
    } else {
        ui_set_utf8(terminal_is_utf8());
        ui_print_batch_tree(&vec, terminal_is_utf8());
    }

    free(vec.items);
    monitor_destroy(mon);
    return rc;
}

int main(int argc, char **argv)
{
    /* So that uname/hostname output and UTF-8 detection honour the user's
     * locale; falls back to the C locale safely. */
    setlocale(LC_ALL, "");

    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[1], "--once") == 0)
            return do_batch_table();
        if (strcmp(argv[1], "--tree") == 0)
            return do_batch_tree();
        if (strcmp(argv[1], "--demo") == 0)
            return run_process_demo();
        fprintf(stderr, "error: unknown option '%s' (try --help)\n", argv[1]);
        return 2;
    }

    /* Interactive monitor: create the monitor once, hand it to the UI. */
    {
        MonitorState *mon;
        ProcVec vec;
        SystemStats stats;
        int rc = 0;

        memset(&vec, 0, sizeof vec);
        memset(&stats, 0, sizeof stats);
        system_info_read_static(&stats);

        mon = monitor_create();
        if (!mon) {
            fprintf(stderr, "error: cannot initialise the monitor\n");
            return 1;
        }
        ui_run(mon, &vec, &stats);
        free(vec.items);
        monitor_destroy(mon);
        return rc;
    }
}
