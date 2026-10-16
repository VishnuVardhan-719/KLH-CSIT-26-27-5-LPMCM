/*
 * process_monitor.c — /proc scanning, parsing and interval resource math.
 *
 * Design notes
 * ------------
 * - Discovery: iterate the numeric directories of /proc (one per PID).
 * - Per process: /proc/<pid>/stat (pid, comm, state, ppid, times, threads,
 *   rss), /proc/<pid>/cmdline (argv), /proc/<pid>/exe (readlink) and the
 *   owner uid from stat("/proc/<pid>").
 * - Races: processes vanish constantly. Every read failure is treated as
 *   "skip this process this round" — the monitor never crashes because
 *   /proc/<pid> disappeared between readdir() and read().
 * - CPU% is a true interval measurement: per-process utime+stime deltas
 *   divided by the total system jiffy delta from /proc/stat. The previous
 *   sample is kept in an open-addressing hash keyed by PID + starttime, so
 *   PID reuse cannot inherit another process's counters. The same
 *   calculation backs every view (table, details, batch output).
 */
#include "process_monitor.h"

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Dynamic process vector                                              */
/* ------------------------------------------------------------------ */

void proc_vec_init(ProcVec *v)
{
    if (!v)
        return;
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

void proc_vec_push(ProcVec *v, const ProcessInfo *pi)
{
    if (!v || !pi)
        return;
    if (v->count == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 256;
        v->items = xrealloc(v->items, ncap * sizeof *v->items);
        v->cap = ncap;
    }
    v->items[v->count++] = *pi;
}

void proc_vec_free(ProcVec *v)
{
    if (!v)
        return;
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

/* ------------------------------------------------------------------ */
/* /proc parsing (pure functions — unit-testable)                      */
/* ------------------------------------------------------------------ */

int parse_stat_line(const char *line, StatFields *f)
{
    const char *lpar, *rpar, *p;
    char pidbuf[32];
    size_t plen, comm_len;
    char *end;
    long pid;
    char state = '?';
    int ppid = 0;
    unsigned long utime = 0, stime = 0, threads = 0, rss = 0;
    unsigned long long starttime = 0;
    long priority = 0, nice = 0;
    int n;

    if (!line || !f)
        return -1;

    lpar = strchr(line, '(');
    rpar = strrchr(line, ')');
    if (!lpar || !rpar || rpar < lpar)
        return -1;

    plen = (size_t)(lpar - line);
    if (plen == 0 || plen >= sizeof pidbuf)
        return -1;
    memcpy(pidbuf, line, plen);
    pidbuf[plen] = '\0';
    /* Real kernel lines carry a blank between the PID and "(comm)". */
    while (plen > 0 && (pidbuf[plen - 1] == ' ' || pidbuf[plen - 1] == '\t'))
        pidbuf[--plen] = '\0';
    if (plen == 0)
        return -1;
    pid = strtol(pidbuf, &end, 10);
    if (*end != '\0' || pid <= 0)
        return -1;

    /* comm may contain spaces and even ')' — the kernel terminates the
     * field with the LAST ')' on the line, then a space, then state. */
    comm_len = (size_t)(rpar - lpar - 1);
    if (comm_len >= sizeof f->comm)
        comm_len = sizeof f->comm - 1;
    memcpy(f->comm, lpar + 1, comm_len);
    f->comm[comm_len] = '\0';

    p = rpar + 1;
    while (*p == ' ' || *p == '\t')
        ++p;

    /* Fields after state (1-based kernel numbering):
     *   4 ppid  5 pgrp  6 session  7 tty  8 tpgid  9 flags
     *  10-13 minflt/cminflt/majflt/cmajflt
     *  14 utime  15 stime  16 cutime  17 cstime
     *  18 priority  19 nice  20 num_threads  21 itrealvalue
     *  22 starttime  23 vsize  24 rss (pages)                        */
    /* Skip fields 5-13 after ppid, read utime/stime, skip child times,
     * read priority/nice/threads, skip itrealvalue, read starttime,
     * skip vsize, then read rss. */
    n = sscanf(p,
               "%c %d %*d %*d %*d %*d %*d %*d %*d %*d %*d"
               " %lu %lu %*d %*d %ld %ld %lu %*d %llu %*d %lu",
               &state, &ppid, &utime, &stime, &priority, &nice, &threads,
               &starttime, &rss);
    if (n != 9)
        return -1;

    f->pid = (int)pid;
    f->state = state;
    f->ppid = ppid;
    f->utime = utime;
    f->stime = stime;
    f->priority = priority;
    f->nice = nice;
    f->num_threads = threads;
    f->starttime = starttime;
    f->rss_pages = rss;
    return 0;
}

int parse_status_field(const char *status, const char *key, char *out,
                       size_t outsz)
{
    const char *line = status;
    size_t klen;
    if (!status || !key || !out || outsz == 0)
        return -1;
    klen = strlen(key);
    while (line && *line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        if (len > klen && strncmp(line, key, klen) == 0 &&
            line[klen] == ':') {
            /* /proc/status separates fields with tabs; a value may contain
             * spaces ("5124 kB") but never tabs within one field. */
            const char *v = line + klen + 1;
            const char *vend;
            size_t vlen;
            while (v < line + len && (*v == ' ' || *v == '\t'))
                ++v;
            vend = memchr(v, '\t', (size_t)(line + len - v));
            vlen = vend ? (size_t)(vend - v) : (size_t)(line + len - v);
            while (vlen > 0 && (v[vlen - 1] == ' ' || v[vlen - 1] == '\r'))
                --vlen;
            if (vlen >= outsz)
                vlen = outsz - 1;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return 0;
        }
        line = eol ? eol + 1 : NULL;
    }
    return -1;
}

const char *state_char_name(char state)
{
    switch (state) {
    case 'R': return "RUNNING";
    case 'S': return "SLEEPING";
    case 'D': return "DISK SLEEP";
    case 'T': return "STOPPED";
    case 't': return "TRACING";
    case 'Z': return "ZOMBIE";
    case 'X': return "DEAD";
    case 'I': return "IDLE";
    case 'P': return "PARKED";
    case 'W': return "WAKING";
    default:  return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Single-process reader                                               */
/* ------------------------------------------------------------------ */

struct MonitorState; /* defined below; fill_memory needs page size + MemTotal */
static void fill_memory_from(long mem_total_kb, unsigned long page_size,
                             ProcessInfo *pi)
{
    double bytes = (double)pi->rss_pages * (double)page_size;
    pi->memory_mb = bytes / (1024.0 * 1024.0);
    pi->memory_percent =
        mem_total_kb > 0
            ? 100.0 * (bytes / 1024.0) / (double)mem_total_kb
            : 0.0;
}

int get_process_info(int pid, ProcessInfo *info)
{
    char path[96];
    char buf[4096];
    struct stat st;
    StatFields sf;
    long clen;

    if (pid <= 0 || !info)
        return -1;

    /* Owner: the /proc/<pid> directory is owned by the process uid. */
    snprintf(path, sizeof path, "/proc/%d", pid);
    if (stat(path, &st) != 0)
        return -1; /* ENOENT: gone, EACCES/EPERM: hidden by the kernel */
    if (!S_ISDIR(st.st_mode))
        return -1;

    memset(info, 0, sizeof *info);
    info->pid = pid;
    info->uid = st.st_uid;
    sstrcpy(info->user, sizeof info->user, user_name(st.st_uid));

    snprintf(path, sizeof path, "/proc/%d/stat", pid);
    if (read_text_file(path, buf, sizeof buf) < 0)
        return -1;
    if (parse_stat_line(buf, &sf) != 0)
        return -1;

    info->ppid = sf.ppid;
    info->state = sf.state;
    info->utime = sf.utime;
    info->stime = sf.stime;
    info->total_ticks = (unsigned long long)sf.utime + sf.stime;
    info->starttime = sf.starttime;
    info->priority = sf.priority;
    info->nice = sf.nice;
    info->threads = sf.num_threads;
    info->rss_pages = sf.rss_pages;
    sstrcpy(info->name, sizeof info->name, sf.comm);
    sstrcpy(info->state_name, sizeof info->state_name,
            state_char_name(sf.state));

    /* Command line: argv joined with NUL separators in /proc. */
    snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
    clen = read_text_file(path, buf, sizeof buf);
    if (clen > 0) {
        long i;
        for (i = 0; i < clen - 1; ++i)
            if (buf[i] == '\0')
                buf[i] = ' ';
        /* buf is NUL-terminated at clen; strip a trailing separator. */
        if (clen >= 2 && buf[clen - 1] == '\0')
            buf[clen - 1] = '\0';
        sstrcpy(info->cmdline, sizeof info->cmdline, buf);
    } else {
        /* Empty cmdline => kernel thread: present it as [comm]. */
        snprintf(info->cmdline, sizeof info->cmdline, "[%s]", sf.comm);
    }

    /* Executable target (permission-denied for other users' processes:
     * the field simply stays empty and views render "-"). */
    snprintf(path, sizeof path, "/proc/%d/exe", pid);
    {
        ssize_t n = readlink(path, info->exe, sizeof info->exe - 1);
        if (n > 0) {
            info->exe[n] = '\0';
        } else {
            info->exe[0] = '\0';
        }
    }

    fill_memory_from(-1, (unsigned long)sysconf(_SC_PAGESIZE), info);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Monitor state: previous-sample tick table + system constants        */
/* ------------------------------------------------------------------ */

typedef struct {
    int pid;
    unsigned long long starttime;
    unsigned long long ticks;
    unsigned long long gen;
    bool used;
} TickSlot;

struct MonitorState {
    long mem_total_kb;
    unsigned long page_size;
    TickSlot *slots;
    size_t cap, used_count;
    unsigned long long pass;
    unsigned long long sys_total_prev, sys_idle_prev;
    bool has_prev;
    long clk_tck;
    long cpu_count;
    /* rolling baseline for the details view */
    int details_pid;
    unsigned long long details_starttime;
    unsigned long long details_ticks;
    unsigned long long details_sys;
    bool details_valid;
};

static void fill_memory(const MonitorState *m, ProcessInfo *pi)
{
    fill_memory_from(m->mem_total_kb, m->page_size, pi);
}

MonitorState *monitor_create(void)
{
    MonitorState *m = xcalloc(1, sizeof *m);
    SystemStats stats;
    m->clk_tck = sysconf(_SC_CLK_TCK);
    m->page_size = (unsigned long)sysconf(_SC_PAGESIZE);
    if (system_info_read_static(&stats) == 0)
        m->mem_total_kb = stats.mem_total_kb;
    m->cpu_count = stats.cpu_count > 0 ? stats.cpu_count : 1;
    return m;
}

void monitor_destroy(MonitorState *m)
{
    if (!m)
        return;
    free(m->slots);
    free(m);
}

static size_t hash_pid(int pid, size_t cap)
{
    unsigned long long h = (unsigned long long)(unsigned)pid * 2654435761ULL;
    return (size_t)(h & (cap - 1));
}

static void tick_rehash(MonitorState *m, size_t ncap);

static void tick_insert(MonitorState *m, int pid,
                        unsigned long long starttime,
                        unsigned long long ticks)
{
    size_t i;
    if (m->cap == 0)
        tick_rehash(m, 64); /* first insert: allocate the initial table */
    else if (m->used_count * 10 >= m->cap * 7)
        tick_rehash(m, m->cap * 2);
    i = hash_pid(pid, m->cap);
    while (m->slots[i].used && m->slots[i].pid != pid)
        i = (i + 1) & (m->cap - 1);
    if (!m->slots[i].used) {
        m->slots[i].used = true;
        m->slots[i].pid = pid;
        ++m->used_count;
    }
    m->slots[i].starttime = starttime;
    m->slots[i].ticks = ticks;
    m->slots[i].gen = m->pass;
}

static bool tick_lookup(const MonitorState *m, int pid,
                         unsigned long long starttime,
                         unsigned long long *ticks)
{
    size_t i;
    if (m->cap == 0)
        return false;
    i = hash_pid(pid, m->cap);
    while (m->slots[i].used) {
        if (m->slots[i].pid == pid && m->slots[i].starttime == starttime) {
            *ticks = m->slots[i].ticks;
            return true;
        }
        i = (i + 1) & (m->cap - 1);
    }
    return false;
}

static void tick_rehash(MonitorState *m, size_t ncap)
{
    TickSlot *old = m->slots;
    if (ncap < 64)
        ncap = 64; /* power of two, never zero */
    size_t oldcap = m->cap, i;
    m->slots = xcalloc(ncap, sizeof *m->slots);
    m->cap = ncap;
    m->used_count = 0;
    for (i = 0; i < oldcap; ++i) {
        if (!old[i].used)
            continue;
        /* re-insert preserving generation */
        {
            size_t j = hash_pid(old[i].pid, ncap);
            while (m->slots[j].used)
                j = (j + 1) & (ncap - 1);
            m->slots[j].pid = old[i].pid;
            m->slots[j].starttime = old[i].starttime;
            m->slots[j].ticks = old[i].ticks;
            m->slots[j].gen = old[i].gen;
            m->slots[j].used = true;
            ++m->used_count;
        }
    }
    free(old);
}

/* Drop entries not seen during the latest scan (processes that exited). */
static void tick_prune(MonitorState *m)
{
    TickSlot *old;
    size_t oldcap, i;
    if (m->cap == 0)
        return;
    old = m->slots;
    oldcap = m->cap;
    m->slots = xcalloc(oldcap, sizeof *m->slots);
    m->used_count = 0;
    for (i = 0; i < oldcap; ++i) {
        if (old[i].used && old[i].gen == m->pass) {
            size_t j = hash_pid(old[i].pid, oldcap);
            while (m->slots[j].used)
                j = (j + 1) & (oldcap - 1);
            m->slots[j] = old[i];
            ++m->used_count;
        }
    }
    free(old);
}

/* ------------------------------------------------------------------ */
/* CPU math                                                            */
/* ------------------------------------------------------------------ */

double cpu_percent_of(unsigned long long proc_prev,
                      unsigned long long proc_now,
                      unsigned long long sys_prev,
                      unsigned long long sys_now)
{
    unsigned long long sdt;
    if (sys_now <= sys_prev)
        return 0.0;
    sdt = sys_now - sys_prev;
    if (proc_now < proc_prev) /* PID reuse or counter reset: no delta */
        return 0.0;
    if (sdt == 0)
        return 0.0;
    return 100.0 * (double)(proc_now - proc_prev) / (double)sdt;
}

/* ------------------------------------------------------------------ */
/* Full scan                                                           */
/* ------------------------------------------------------------------ */

size_t monitor_refresh(MonitorState *m, ProcVec *vec, SystemStats *stats)
{
    unsigned long long sys_total = 0, sys_idle = 0;
    DIR *d;
    struct dirent *de;

    if (!m || !vec || !stats)
        return 0;

    if (read_system_cpu(&sys_total, &sys_idle) != 0)
        return (size_t)-1;

    vec->count = 0;
    ++m->pass;

    d = opendir("/proc");
    if (!d) {
        fprintf(stderr, "error: cannot open /proc: %s\n", strerror(errno));
        return (size_t)-1;
    }
    while ((de = readdir(d)) != NULL) {
        char *end;
        long pid;
        ProcessInfo pi;
        unsigned long long prev_ticks = 0;

        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        pid = strtol(de->d_name, &end, 10);
        if (*end != '\0' || pid <= 0)
            continue;

        if (get_process_info((int)pid, &pi) != 0)
            continue; /* vanished mid-scan or unreadable: skip this round */

        pi.cpu_percent = 0.0;
        if (m->has_prev &&
            tick_lookup(m, pi.pid, pi.starttime, &prev_ticks))
            pi.cpu_percent =
                cpu_percent_of(prev_ticks, pi.total_ticks,
                               m->sys_total_prev, sys_total) * m->cpu_count;
        tick_insert(m, pi.pid, pi.starttime, pi.total_ticks);

        fill_memory(m, &pi);
        proc_vec_push(vec, &pi);
    }
    closedir(d);

    /* System-wide interval CPU% (same window as the per-process deltas). */
    if (m->has_prev && sys_total > m->sys_total_prev) {
        unsigned long long tdt = sys_total - m->sys_total_prev;
        unsigned long long idt =
            sys_idle > m->sys_idle_prev ? sys_idle - m->sys_idle_prev : 0;
        stats->cpu_percent =
            tdt > 0 ? 100.0 * (double)(tdt > idt ? tdt - idt : 0) /
                          (double)tdt
                    : 0.0;
    } else {
        stats->cpu_percent = 0.0;
    }
    m->sys_total_prev = sys_total;
    m->sys_idle_prev = sys_idle;
    m->has_prev = true;
    tick_prune(m);

    system_info_read_dynamic(stats);
    stats->process_count = vec->count;

    /* Deterministic default ordering until the user picks a sort. */
    sort_processes(vec, SORT_PID, false);
    return vec->count;
}

int monitor_sample_details(MonitorState *m, int pid, ProcessInfo *info)
{
    unsigned long long sys_now = 0, idle_now = 0;

    if (!m || get_process_info(pid, info) != 0)
        return -1;
    fill_memory(m, info);
    info->cpu_percent = 0.0;

    if (read_system_cpu(&sys_now, &idle_now) == 0) {
        if (!m->details_valid || m->details_pid != pid ||
            m->details_starttime != info->starttime ||
            sys_now <= m->details_sys) {
            /* first sample for this PID: establish the baseline */
            m->details_pid = pid;
            m->details_starttime = info->starttime;
            m->details_ticks = info->total_ticks;
            m->details_sys = sys_now;
            m->details_valid = true;
        } else {
            info->cpu_percent =
                cpu_percent_of(m->details_ticks, info->total_ticks,
                               m->details_sys, sys_now) * m->cpu_count;
            m->details_ticks = info->total_ticks;
            m->details_sys = sys_now;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sorting, searching, filtering                                       */
/* ------------------------------------------------------------------ */

static SortKey g_sort_key;
static bool g_sort_desc;

static int cmp_state(const ProcessInfo *a, const ProcessInfo *b)
{
    return strcmp(state_char_name(a->state), state_char_name(b->state));
}

static int cmp_for(const void *pa, const void *pb)
{
    const ProcessInfo *a = pa;
    const ProcessInfo *b = pb;
    int c = 0;
    switch (g_sort_key) {
    case SORT_PID:
        c = (a->pid > b->pid) - (a->pid < b->pid);
        break;
    case SORT_NAME:
        c = strcmp(a->name, b->name);
        break;
    case SORT_CPU:
        c = (a->cpu_percent > b->cpu_percent) -
            (a->cpu_percent < b->cpu_percent);
        break;
    case SORT_MEM:
        c = (a->memory_mb > b->memory_mb) - (a->memory_mb < b->memory_mb);
        break;
    case SORT_STATE:
        c = cmp_state(a, b);
        break;
    case SORT_USER:
        c = strcmp(a->user, b->user);
        break;
    }
    if (g_sort_desc && c != 0)
        c = -c;
    /* deterministic tie-break: ascending PID */
    if (c == 0)
        c = (a->pid > b->pid) - (a->pid < b->pid);
    return c;
}

void sort_processes(ProcVec *v, SortKey key, bool descending)
{
    if (!v || v->count < 2)
        return;
    g_sort_key = key;
    g_sort_desc = descending;
    qsort(v->items, v->count, sizeof *v->items, cmp_for);
}

int process_matches(const ProcessInfo *pi, const char *query)
{
    char pidbuf[16];
    if (!pi)
        return 0;
    if (!query || query[0] == '\0')
        return 1;
    if (ci_contains(pi->name, query) || ci_contains(pi->user, query) ||
        ci_contains(pi->cmdline, query))
        return 1;
    snprintf(pidbuf, sizeof pidbuf, "%d", pi->pid);
    return strstr(pidbuf, query) != NULL;
}

int process_matches_state(const ProcessInfo *pi, char state)
{
    if (!pi)
        return 0;
    if (state == '\0' || state == 'A')
        return 1;
    return pi->state == state;
}
