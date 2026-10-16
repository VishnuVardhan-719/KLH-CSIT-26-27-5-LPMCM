/*
 * utils.h — shared helpers for the Linux Process Monitoring & Control System.
 *
 * Every helper here is used by more than one module. Keeping them in one
 * place prevents duplicated /proc parsing or string handling logic.
 */
#ifndef LPMCS_UTILS_H
#define LPMCS_UTILS_H

#include <stddef.h>
#include <sys/types.h>

/* Fixed capacities of the central process model. */
#define MAX_NAME     128   /* process (comm) name */
#define MAX_USER      64   /* owning user name    */
#define MAX_CMDLINE 1024   /* joined command line */
#define MAX_EXE      512   /* executable path     */

/*
 * Allocation wrappers. On out-of-memory they print an error and terminate:
 * the monitor cannot continue meaningfully without its allocations, and
 * dying cleanly with a message is the graceful behaviour available to a
 * single-process C program under OOM. No silent NULL-deref crashes.
 */
void *xmalloc(size_t size);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *ptr, size_t size);

/* Safe string copy: always NUL-terminates, never overflows dst. */
void sstrcpy(char *dst, size_t dstsz, const char *src);

/* Strip leading/trailing blanks in place. Returns dst. */
char *trim(char *s);

/* Case-insensitive substring search. NULL-safe; NULL needle -> 0. */
int ci_contains(const char *haystack, const char *needle);

/* Display width of a UTF-8 string in terminal cells (approximate:
 * counts code points, treating all as single-width). */
size_t utf8_width(const char *s);

/* Copy at most max_width display cells of src into dst (NUL-terminated,
 * cut only on code-point boundaries). Returns number of cells copied. */
size_t utf8_copy(char *dst, size_t dstsz, const char *src, size_t max_width);

/* Read a text file (e.g. /proc/<pid>/stat) into buf, NUL-terminated.
 * Returns number of bytes read, or -1 on error (ENOENT, EACCES, ...). */
long read_text_file(const char *path, char *buf, size_t bufsz);

/* Format an uptime in seconds as "DDd HH:MM:SS" or "HH:MM:SS". */
void fmt_uptime(double seconds, char *buf, size_t bufsz);

/* Resolve a uid to a user name ("root", "vishnu", ...). Returns "" when
 * the uid has no passwd entry (displayed as "-"). Results are cached. */
const char *user_name(uid_t uid);

#endif /* LPMCS_UTILS_H */
