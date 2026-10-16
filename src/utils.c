/*
 * utils.c — implementation of shared helpers.
 */
#include "utils.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p) {
        fprintf(stderr, "fatal: out of memory (%zu bytes): %s\n",
                size, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return p;
}

void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) {
        fprintf(stderr, "fatal: out of memory (%zu x %zu bytes): %s\n",
                n, size, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return p;
}

void *xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size ? size : 1);
    if (!p) {
        fprintf(stderr, "fatal: out of memory (%zu bytes): %s\n",
                size, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return p;
}

void sstrcpy(char *dst, size_t dstsz, const char *src)
{
    size_t i;
    if (!dst || dstsz == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1 < dstsz && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

char *trim(char *s)
{
    char *end;
    if (!s)
        return s;
    while (*s == ' ' || *s == '\t')
        ++s;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        --end;
    *end = '\0';
    return s;
}

static int ci_lower(int c)
{
    return tolower((unsigned char)c);
}

int ci_contains(const char *haystack, const char *needle)
{
    size_t nlen;
    if (!haystack || !needle)
        return 0;
    nlen = strlen(needle);
    if (nlen == 0)
        return 1;
    for (; *haystack; ++haystack) {
        size_t i = 0;
        while (i < nlen &&
               ci_lower((unsigned char)haystack[i]) ==
               ci_lower((unsigned char)needle[i]))
            ++i;
        if (i == nlen)
            return 1;
    }
    return 0;
}

size_t utf8_width(const char *s)
{
    size_t w = 0;
    if (!s)
        return 0;
    for (; *s; ++s)
        if (((unsigned char)*s & 0xC0) != 0x80) /* skip continuation bytes */
            ++w;
    return w;
}

size_t utf8_copy(char *dst, size_t dstsz, const char *src, size_t max_width)
{
    size_t w = 0, i = 0;
    if (!dst || dstsz == 0)
        return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    while (src[i] && w < max_width && i + 1 < dstsz) {
        if (((unsigned char)src[i] & 0xC0) != 0x80)
            ++w;
        dst[i] = src[i];
        ++i;
    }
    /* never cut inside a multi-byte sequence: back off continuations */
    while (i > 0 && ((unsigned char)src[i] & 0xC0) == 0x80 && i < dstsz) {
        dst[i] = src[i];
        ++i;
        break; /* only complete the final code point if bytes remain */
    }
    dst[i] = '\0';
    return w;
}

long read_text_file(const char *path, char *buf, size_t bufsz)
{
    FILE *fp;
    size_t off = 0;
    int err;
    if (!path || !buf || bufsz < 2)
        return -1;
    fp = fopen(path, "r");
    if (!fp)
        return -1;
    while (off + 1 < bufsz) {
        size_t n = fread(buf + off, 1, bufsz - 1 - off, fp);
        if (n == 0)
            break;
        off += n;
    }
    buf[off] = '\0';
    err = ferror(fp);
    fclose(fp);
    return err ? -1 : (long)off;
}

void fmt_uptime(double seconds, char *buf, size_t bufsz)
{
    long total, d, hh, mm, ss;
    if (!buf || bufsz == 0)
        return;
    if (seconds < 0)
        seconds = 0;
    total = (long)seconds;
    d  = total / 86400;
    hh = (total % 86400) / 3600;
    mm = (total % 3600) / 60;
    ss = total % 60;
    if (d > 0)
        snprintf(buf, bufsz, "%ldd %02ld:%02ld:%02ld", d, hh, mm, ss);
    else
        snprintf(buf, bufsz, "%02ld:%02ld:%02ld", hh, mm, ss);
}

const char *user_name(uid_t uid)
{
    /* Small cache: the monitor rescans /proc every second and would
     * otherwise repeat the same passwd lookups hundreds of times. */
    static struct { uid_t uid; char name[MAX_USER]; } cache[128];
    static size_t used = 0;
    size_t i;
    char pwbuf[4096];
    struct passwd pw, *res = NULL;

    for (i = 0; i < used; ++i)
        if (cache[i].uid == uid)
            return cache[i].name;

    if (getpwuid_r(uid, &pw, pwbuf, sizeof pwbuf, &res) != 0 ||
        res == NULL || res->pw_name == NULL)
        return "";

    if (used < sizeof cache / sizeof cache[0]) {
        cache[used].uid = uid;
        sstrcpy(cache[used].name, sizeof cache[used].name, res->pw_name);
        ++used;
        return cache[used - 1].name;
    }
    return res->pw_name;
}
