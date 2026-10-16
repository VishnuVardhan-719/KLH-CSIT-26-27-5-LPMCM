/*
 * process_monitor.h — the central process model and /proc monitoring engine.
 *
 * This module is the single source of /proc parsing for the whole program:
 * the process table, the details view, the process tree and the system
 * statistics all consume its data. It never parses /proc twice.
 *
 * CPU usage is measured over a real sampling interval (delta of per-process
 * utime+stime divided by aggregate system-jiffy delta, then scaled by the
 * online CPU count) like top's per-process convention. No lifetime shares.
 */
#ifndef LPMCS_PROCESS_MONITOR_H
#define LPMCS_PROCESS_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "system_info.h"
#include "utils.h"

/* Raw fields parsed from one /proc/<pid>/stat line (testable in isolation). */
typedef struct {
    int pid;
    char comm[MAX_NAME];
    char state;
    int ppid;
    unsigned long utime;
    unsigned long stime;
    long priority;
    long nice;
    unsigned long num_threads;
    unsigned long long starttime;
    unsigned long rss_pages;
} StatFields;

/* The central process model. Every view renders subsets of this. */
typedef struct {
    int pid;
    int ppid;
    char name[MAX_NAME];        /* kernel comm (or argv[0] fallback) */
    char state;                 /* raw state code: R S D T Z I ...   */
    char state_name[20];        /* human-readable state              */
    uid_t uid;
    char user[MAX_USER];
    unsigned long utime, stime; /* clock ticks                       */
    unsigned long long total_ticks; /* utime + stime                 */
    unsigned long long starttime;   /* /proc stat field 22; PID identity */
    long priority;
    long nice;
    unsigned long threads;
    unsigned long rss_pages;
    double memory_mb;
    double memory_percent;
    double cpu_percent;         /* measured over the sample interval */
    char cmdline[MAX_CMDLINE];  /* NULs of argv joined with spaces   */
    char exe[MAX_EXE];          /* readlink(/proc/<pid>/exe), may be empty */
} ProcessInfo;

/* Growable heap array of ProcessInfo (no fixed 4096-entry stack arrays). */
typedef struct {
    ProcessInfo *items;
    size_t count;
    size_t cap;
} ProcVec;

void proc_vec_init(ProcVec *v);
void proc_vec_push(ProcVec *v, const ProcessInfo *pi); /* aborts on OOM */
void proc_vec_free(ProcVec *v);

/* Sort keys for the process table (deterministic, PID tie-break). */
typedef enum {
    SORT_PID = 0,
    SORT_NAME,
    SORT_CPU,
    SORT_MEM,
    SORT_STATE,
    SORT_USER
} SortKey;

void sort_processes(ProcVec *v, SortKey key, bool descending);

/* Persistent monitor state: previous sample snapshot + system constants. */
typedef struct MonitorState MonitorState;

MonitorState *monitor_create(void);
void monitor_destroy(MonitorState *m);

/*
 * Scan /proc and refresh *vec (reused capacity) and *stats.
 * Returns vec->count, or -1 if /proc itself cannot be read.
 * Processes that disappear mid-scan are skipped silently (real race).
 */
size_t monitor_refresh(MonitorState *m, ProcVec *vec, SystemStats *stats);

/* Read one process freshly by PID (no CPU% — use monitor_sample_details
 * for an interval-based value). Returns 0 on success, -1 if the process
 * does not exist / cannot be read (ENOENT, EACCES, malformed data). */
int get_process_info(int pid, ProcessInfo *info);

/*
 * Interval-based CPU% for the details view: keeps a rolling baseline per
 * observed PID. First call reports 0.0 (no interval yet); each subsequent
 * call reports the true percentage over the elapsed window.
 */
int monitor_sample_details(MonitorState *m, int pid, ProcessInfo *info);

/* Pure helpers, exposed for unit tests and shared by views. */
int parse_stat_line(const char *line, StatFields *f);
int parse_status_field(const char *status, const char *key, char *out,
                       size_t outsz);
double cpu_percent_of(unsigned long long proc_prev,
                      unsigned long long proc_now,
                      unsigned long long sys_prev,
                      unsigned long long sys_now);
const char *state_char_name(char state);

/* Search / filter predicates operating on real process data. */
int process_matches(const ProcessInfo *pi, const char *query);
int process_matches_state(const ProcessInfo *pi, char state);

#endif /* LPMCS_PROCESS_MONITOR_H */
