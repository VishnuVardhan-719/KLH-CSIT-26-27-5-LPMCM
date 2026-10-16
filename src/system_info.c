/*
 * system_info.c — reads real system information from the running kernel.
 */
#include "system_info.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

long parse_meminfo_kb(const char *buf, const char *key)
{
    const char *line = buf;
    size_t klen = strlen(key);
    if (!buf || !key)
        return -1;
    while (line && *line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        if (len > klen && strncmp(line, key, klen) == 0 &&
            line[klen] == ':') {
            /* value is "  12345 kB" */
            char val[64];
            size_t vlen = len - klen - 1;
            if (vlen >= sizeof val)
                vlen = sizeof val - 1;
            memcpy(val, line + klen + 1, vlen);
            val[vlen] = '\0';
            return strtol(trim(val), NULL, 10);
        }
        line = eol ? eol + 1 : NULL;
    }
    return -1;
}

int read_system_cpu(unsigned long long *total, unsigned long long *idle)
{
    char buf[512];
    unsigned long long user, nice, sysv, idlev, iowait, irq, softirq, steal;
    if (read_text_file("/proc/stat", buf, sizeof buf) < 0)
        return -1;
    if (sscanf(buf,
               "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &sysv, &idlev, &iowait, &irq, &softirq,
               &steal) < 4)
        return -1;
    if (total)
        *total = user + nice + sysv + idlev + iowait + irq + softirq + steal;
    if (idle)
        *idle = idlev + iowait;
    return 0;
}

int system_info_read_static(SystemStats *s)
{
    struct utsname un;
    char membuf[4096];
    long total_kb = -1;

    if (!s)
        return -1;
    memset(s, 0, sizeof *s);

    if (gethostname(s->hostname, sizeof s->hostname - 1) != 0)
        sstrcpy(s->hostname, sizeof s->hostname, "unknown");

    if (uname(&un) == 0)
        sstrcpy(s->kernel, sizeof s->kernel, un.release);
    else
        sstrcpy(s->kernel, sizeof s->kernel, "unknown");

    s->cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (s->cpu_count < 1)
        s->cpu_count = 1;

    if (read_text_file("/proc/meminfo", membuf, sizeof membuf) >= 0)
        total_kb = parse_meminfo_kb(membuf, "MemTotal");
    s->mem_total_kb = total_kb;
    return total_kb > 0 ? 0 : -1;
}

void system_info_read_dynamic(SystemStats *s)
{
    char buf[512];
    if (!s)
        return;

    if (read_text_file("/proc/uptime", buf, sizeof buf) >= 0)
        s->uptime_seconds = strtod(buf, NULL);
    else
        s->uptime_seconds = 0.0;

    if (read_text_file("/proc/loadavg", buf, sizeof buf) >= 0)
        sscanf(buf, "%lf %lf %lf", &s->load[0], &s->load[1], &s->load[2]);

    {
        char membuf[4096];
        if (read_text_file("/proc/meminfo", membuf, sizeof membuf) >= 0) {
            long avail = parse_meminfo_kb(membuf, "MemAvailable");
            long total = s->mem_total_kb;
            if (avail < 0)
                avail = parse_meminfo_kb(membuf, "MemFree");
            s->mem_available_kb = avail;
            if (total > 0 && avail >= 0) {
                long used_kb = total - avail;
                s->mem_used_mb = (double)used_kb / 1024.0;
                s->mem_used_percent =
                    100.0 * (double)used_kb / (double)total;
            } else {
                s->mem_used_mb = 0.0;
                s->mem_used_percent = 0.0;
            }
        }
    }
}
