/*
 * system_info.h — real system information read from the running kernel.
 *
 * Everything reported here comes from the Linux kernel itself (uname,
 * gethostname, /proc/uptime, /proc/loadavg, /proc/meminfo, /proc/stat).
 * No value is hardcoded.
 */
#ifndef LPMCS_SYSTEM_INFO_H
#define LPMCS_SYSTEM_INFO_H

#include <stddef.h>

#define SYS_HOSTNAME_MAX 128
#define SYS_KERNEL_MAX   128

typedef struct {
    char hostname[SYS_HOSTNAME_MAX];
    char kernel[SYS_KERNEL_MAX];      /* uname() release, e.g. 6.8.0-45 */
    long cpu_count;                   /* online CPUs (sysconf)          */
    long mem_total_kb;                /* /proc/meminfo MemTotal         */
    long mem_available_kb;            /* /proc/meminfo MemAvailable     */
    double uptime_seconds;            /* /proc/uptime                   */
    double load[3];                   /* /proc/loadavg                  */
    double mem_used_mb;
    double mem_used_percent;
    double cpu_percent;               /* system-wide, interval-based    */
    size_t process_count;
} SystemStats;

/* One-time fields: hostname, kernel, cpu count, total memory. Returns 0 on
 * success, -1 if the kernel reports no total memory (monitor still works,
 * memory percentages are then reported as 0). */
int system_info_read_static(SystemStats *s);

/* Volatile fields: uptime, load, available memory. Cheap; call per refresh. */
void system_info_read_dynamic(SystemStats *s);

/* Parse a /proc/meminfo-style buffer for "<Key>:  <n> kB". Returns kB or -1.
 * Exposed for unit tests. */
long parse_meminfo_kb(const char *buf, const char *key);

/* Interval CPU totals from /proc/stat (first "cpu" line).
 * *total = sum of all jiffies, *idle = idle + iowait. 0 on success. */
int read_system_cpu(unsigned long long *total, unsigned long long *idle);

#endif /* LPMCS_SYSTEM_INFO_H */
