/*
 * terminal_ui.c — raw-mode ANSI terminal engine and interactive views.
 *
 * Frame strategy: every frame is composed in a growable memory buffer and
 * flushed with a single write(), positioning the cursor home first. No
 * clear-screen escape is emitted during live updates, so the display does
 * not flicker even at a 1 s refresh cadence.
 *
 * Terminal restore is guaranteed: raw mode + alternate screen are undone
 * on normal quit, on SIGINT/SIGTERM/SIGHUP (via a stop flag checked each
 * loop iteration — no unsafe work inside the handler), and on error paths.
 */
#include "terminal_ui.h"

#include "process_control.h"
#include "process_demo.h"
#include "process_tree.h"
#include "utils.h"

#include <ctype.h>
#include <errno.h>
#include <langinfo.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define UI_REFRESH_MS 1000
#define UI_MIN_COLS   80
#define UI_MIN_ROWS   24
#define MAX_SEARCH    64

/* ------------------------------------------------------------------ */
/* Small ANSI/style toolkit                                            */
/* ------------------------------------------------------------------ */

#define A_RESET "\x1b[0m"
#define A_BOLD  "\x1b[1m"
#define A_DIM   "\x1b[2m"
#define A_REV   "\x1b[7m"
#define A_CYAN  "\x1b[36m"
#define A_GREEN "\x1b[32m"
#define A_YELL  "\x1b[33m"
#define A_RED   "\x1b[31m"
#define A_MAG   "\x1b[35m"
#define A_BLUE  "\x1b[34m"
#define A_GREY  "\x1b[90m"

static volatile sig_atomic_t g_stop = 0;
static int g_utf8 = 1; /* recomputed at startup from the locale */

void ui_request_stop(void) { g_stop = 1; }

/* Termination signals only raise the stop flag (async-signal-safe);
 * the main loop notices it and restores the terminal before exiting. */
static void on_termination_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_termination_signal;
    sigaction(SIGINT, &sa, NULL);  /* Ctrl-C (ISIG stays enabled) */
    sigaction(SIGTERM, &sa, NULL); /* kill / session shutdown      */
    sigaction(SIGHUP, &sa, NULL);  /* terminal closed              */
}

void ui_set_utf8(int enabled) { g_utf8 = enabled ? 1 : 0; }

static const char *bx(const char *uni, const char *ascii)
{
    return g_utf8 ? uni : ascii;
}

static const char *state_color(char state)
{
    switch (state) {
    case 'R': return A_GREEN A_BOLD;
    case 'S': return A_BLUE;
    case 'D': return A_YELL;
    case 'T': return A_MAG;
    case 't': return A_MAG;
    case 'Z': return A_RED A_BOLD;
    case 'X': return A_RED;
    case 'I': return A_GREY;
    default:  return A_GREY;
    }
}

/* ------------------------------------------------------------------ */
/* Terminal mode management                                            */
/* ------------------------------------------------------------------ */

static struct termios g_orig_termios;
static int g_raw_active = 0;
static int g_alt_active = 0;

static void write_str(const char *s)
{
    size_t len = strlen(s);
    ssize_t rc = write(STDOUT_FILENO, s, len);
    (void)rc; /* best effort; nothing sensible to do on failure */
}

static void terminal_leave_alt(void)
{
    if (g_alt_active) {
        write_str("\x1b[?1049l\x1b[?25h\x1b[0m");
        g_alt_active = 0;
    }
}

static void terminal_restore(void)
{
    if (g_raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_active = 0;
    }
    terminal_leave_alt();
}

static void detect_utf8(void);

static int terminal_enter(void)
{
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0)
        return -1;
    if (!isatty(STDIN_FILENO))
        return -1;
    detect_utf8();
    {
        struct termios t = g_orig_termios;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO); /* keep ISIG for Ctrl-C */
        t.c_iflag &= ~(tcflag_t)(IXON);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0)
            return -1;
    }
    g_raw_active = 1;
    write_str("\x1b[?1049h\x1b[?25l"); /* alt screen + hide cursor */
    g_alt_active = 1;
    return 0;
}

static void detect_utf8(void)
{
    const char *cs = nl_langinfo(CODESET);
    g_utf8 = (cs && (strstr(cs, "UTF") != NULL || strstr(cs, "utf") != NULL))
                 ? 1
                 : 0;
}

/* ------------------------------------------------------------------ */
/* Key input                                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    K_NONE = 0,
    K_UP,
    K_DOWN,
    K_PGUP,
    K_PGDN,
    K_HOME,
    K_END,
    K_ENTER,
    K_ESC,
    K_BACKSPACE,
    K_CHAR
} KeyKind;

typedef struct {
    KeyKind kind;
    char ch;
} Key;

/* Returns 1 when a key was parsed, 0 on timeout/EINTR, -1 on hard error. */
/*
 * Input queue: a single read(2) can return a burst of several keys
 * (fast typing, held arrow key). Every byte is decoded and queued;
 * read_key() pops one key at a time so no input is ever dropped.
 */
#define KEYQ_MAX 256
static unsigned char g_keyq[KEYQ_MAX];
static size_t g_keyq_len;

static void keyq_push(const unsigned char *bytes, size_t n)
{
    while (n--) {
        if (g_keyq_len < KEYQ_MAX)
            g_keyq[g_keyq_len++] = *bytes++;
        else
            break; /* queue full: drop overflow (pathological input) */
    }
}

/* Decode one key from the queue head. Returns 1 when a key was decoded. */
static int keyq_pop(Key *k)
{
    unsigned char c;
    if (g_keyq_len == 0)
        return 0;
    c = g_keyq[0];

    if (c == 0x1b) {
        if (g_keyq_len == 1) {
            k->kind = K_ESC;
            --g_keyq_len;
            return 1;
        }
        if (g_keyq_len >= 3 && g_keyq[1] == '[') {
            char f = (char)g_keyq[2];
            int two = (g_keyq_len >= 4 &&
                       (f == '5' || f == '6' || f == '1' || f == '4') &&
                       g_keyq[3] == '~');
            size_t cons = two ? 4 : 3;
            switch (f) {
            case 'A': k->kind = K_UP; break;
            case 'B': k->kind = K_DOWN; break;
            case 'H': k->kind = K_HOME; break;
            case 'F': k->kind = K_END; break;
            case '5': if (two) { k->kind = K_PGUP; break; } cons = 3; break;
            case '6': if (two) { k->kind = K_PGDN; break; } cons = 3; break;
            case '1': if (two) { k->kind = K_HOME; break; } cons = 3; break;
            case '4': if (two) { k->kind = K_END; break; } cons = 3; break;
            default:  cons = 3; break; /* unknown CSI: swallow 3 bytes */
            }
            memmove(g_keyq, g_keyq + cons, g_keyq_len - cons);
            g_keyq_len -= cons;
            return k->kind != K_NONE;
        }
        /* ESC + something unrecognised: consume the escape and the next
         * byte (e.g. ESC O P function keys), report nothing. */
        memmove(g_keyq, g_keyq + 2, g_keyq_len - 2);
        g_keyq_len -= 2;
        return 0;
    }

    /* single-byte keys */
    memmove(g_keyq, g_keyq + 1, g_keyq_len - 1);
    --g_keyq_len;
    if (c == '\r' || c == '\n') {
        k->kind = K_ENTER;
        return 1;
    }
    if (c == 0x7f || c == 0x08) {
        k->kind = K_BACKSPACE;
        return 1;
    }
    if (c >= 32 && c < 127) {
        k->kind = K_CHAR;
        k->ch = (char)c;
        return 1;
    }
    return 0; /* other control byte: swallowed */
}

static int read_key(int timeout_ms, Key *k)
{
    struct pollfd pfd;
    int pr;
    unsigned char buf[64];
    ssize_t n;

    k->kind = K_NONE;
    k->ch = 0;

    /* keys decoded earlier wait in the queue — drain without blocking */
    if (g_keyq_len > 0)
        return keyq_pop(k);

    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pr = poll(&pfd, 1, timeout_ms);
    if (pr < 0)
        return (errno == EINTR) ? 0 : -1;
    if (pr == 0)
        return 0;

    do {
        n = read(STDIN_FILENO, buf, sizeof buf);
    } while (n < 0 && errno == EINTR);
    if (n <= 0)
        return (n == 0) ? 0 : ((errno == EINTR) ? 0 : -1);

    keyq_push(buf, (size_t)n);
    return keyq_pop(k);
}

/* ------------------------------------------------------------------ */
/* Frame buffer                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t len, cap;
} FrameBuf;

static void fb_init(FrameBuf *f)
{
    f->cap = 64 * 1024;
    f->buf = xmalloc(f->cap);
    f->len = 0;
}

static void fb_free(FrameBuf *f)
{
    free(f->buf);
    f->buf = NULL;
    f->len = f->cap = 0;
}

static void fb_raw(FrameBuf *f, const char *s)
{
    size_t n = strlen(s);
    while (f->len + n + 1 > f->cap) {
        f->cap *= 2;
        f->buf = xrealloc(f->buf, f->cap);
    }
    memcpy(f->buf + f->len, s, n);
    f->len += n;
    f->buf[f->len] = '\0';
}

static void fb_fmt(FrameBuf *f, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    fb_raw(f, tmp);
}

static void fb_flush(FrameBuf *f)
{
    ssize_t rc = write(STDOUT_FILENO, f->buf, f->len);
    (void)rc;
}

/*
 * Flicker-free output: keep the previous frame's plain text per row and
 * emit only the rows whose content changed (plus their positioning).
 * This is the big anti-jitter win on Windows Terminal/conpty, where a
 * full-screen repaint every second is rendered with visible flicker.
 */
static struct {
    char **rows;   /* previous frame's plain text, 1-based [1..h]    */
    int w, h;      /* geometry the cache was built for               */
    int valid;     /* 0 until the first full paint                   */
} g_prev;

static void prev_cache_reset(int w, int h)
{
    int r;
    if (g_prev.rows && (g_prev.w != w || g_prev.h != h)) {
        for (r = 1; r <= g_prev.h; ++r)
            free(g_prev.rows[r]);
        free(g_prev.rows);
        g_prev.rows = NULL;
        g_prev.valid = 0;
    }
    if (!g_prev.rows) {
        g_prev.rows = xcalloc((size_t)h + 1, sizeof *g_prev.rows);
        g_prev.w = w;
        g_prev.h = h;
    }
}

static void prev_cache_free(void)
{
    int r;
    if (!g_prev.rows)
        return;
    for (r = 1; r <= g_prev.h; ++r)
        free(g_prev.rows[r]);
    free(g_prev.rows);
    g_prev.rows = NULL;
    g_prev.valid = 0;
}

/* strip CSI/escape sequences so only visible text is compared */
static void strip_escapes(const char *src, char *dst, size_t dstsz)
{
    size_t o = 0;
    while (*src && o + 1 < dstsz) {
        if (*src == '\x1b') {
            ++src;
            if (*src == '[') {
                ++src;
                while (*src && (*src == ';' || *src == '?' ||
                                (*src >= '0' && *src <= '9')))
                    ++src;
                if (*src)
                    ++src; /* final byte */
            } else if (*src) {
                ++src;
            }
            continue;
        }
        dst[o++] = *src++;
    }
 dst[o] = '\0';
}

/* Return the next "ESC[<row>;1H" marker in [r,end), correctly skipping
 * other escape sequences. Returns end when none remains. */
static const char *scan_next_row_marker(const char *r, const char *end)
{
    while (r < end) {
        if (r[0] == '\x1b' && end - r > 2 && r[1] == '[') {
            const char *d = r + 2;
            while (d < end && *d >= '0' && *d <= '9')
                ++d;
            if (d < end && *d == ';' && end - d > 2 && d[1] == '1' &&
                d[2] == 'H')
                return r; /* a row marker */
            ++d; /* final byte of some other escape: skip it */
            r = d;
            continue;
        }
        ++r;
    }
    return end;
}

static void fb_flush_diff(FrameBuf *f, int w, int h)
{
    const char *p = f->buf;
    const char *end = f->buf + f->len;
    char plain[4096];
    int first = 1;

    prev_cache_reset(w, h);

    while (p < end) {
        const char *marker = scan_next_row_marker(p, end);
        if (marker == end)
            break;
        {
            char *q;
            long row = strtol(marker + 2, &q, 10);
            const char *rstart = q + 3; /* past ";1H" */
            const char *rend = scan_next_row_marker(rstart, end);
            size_t n = (size_t)(rend - rstart);
            char *seg = xmalloc(n + 1);
            int changed;
            memcpy(seg, rstart, n);
            seg[n] = '\0';
            strip_escapes(seg, plain, sizeof plain);
            changed = !g_prev.valid || !g_prev.rows[row] ||
                      strcmp(g_prev.rows[row], plain) != 0;
            if (changed && row >= 1 && row <= h) {
                char pos[32];
                int pn = snprintf(pos, sizeof pos, "\x1b[%ld;1H", row);
                ssize_t rc = write(STDOUT_FILENO, pos, (size_t)pn);
                (void)rc;
                rc = write(STDOUT_FILENO, seg, n);
                (void)rc;
                free(g_prev.rows[row]);
                g_prev.rows[row] = xmalloc(strlen(plain) + 1);
                strcpy(g_prev.rows[row], plain);
            }
            free(seg);
            first = 0;
            p = rend;
        }
    }
    g_prev.valid = 1;
    /* bytes before the first marker are one-shot setup (home, alt-screen
     * enter); they were already written when this frame was first drawn */
    (void)first;
}

/* force the next flush to repaint every row (mode changes, popups) */
static void fb_invalidate(void)
{
    g_prev.valid = 0;
}

/* Line writer: composes one full-width row with style runs and padding. */
typedef struct {
    FrameBuf *fb;
    int row;
    int width;
    size_t used;
} LineWriter;

static void lw_begin(LineWriter *lw, FrameBuf *fb, int row, int width)
{
    lw->fb = fb;
    lw->row = row;
    lw->width = width;
    lw->used = 0;
    fb_fmt(fb, "\x1b[%d;1H", row);
}

static void lw_text(LineWriter *lw, const char *style, const char *text)
{
    char clipped[1024];
    size_t room, w;
    if (lw->used >= (size_t)lw->width)
        return;
    room = (size_t)lw->width - lw->used;
    w = utf8_copy(clipped, sizeof clipped, text, room);
    lw->used += w;
    fb_raw(lw->fb, style);
    fb_raw(lw->fb, clipped);
    fb_raw(lw->fb, A_RESET);
}

static void lw_textf(LineWriter *lw, const char *style, const char *fmt,
                     ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    lw_text(lw, style, tmp);
}

static void lw_pad(LineWriter *lw, const char *style)
{
    while (lw->used < (size_t)lw->width) {
        fb_raw(lw->fb, style);
        fb_raw(lw->fb, " ");
        lw->used++;
    }
    if (lw->used > (size_t)lw->width)
        lw->used = (size_t)lw->width;
}

/* Pad up to an absolute column so successive cells align. */
static void lw_pad_to(LineWriter *lw, size_t target, const char *style)
{
    if (target > (size_t)lw->width)
        target = (size_t)lw->width;
    while (lw->used < target) {
        fb_raw(lw->fb, style);
        fb_raw(lw->fb, " ");
        lw->used++;
    }
}

/* ------------------------------------------------------------------ */
/* UI state                                                            */
/* ------------------------------------------------------------------ */

typedef enum {
    MODE_TABLE,
    MODE_DETAILS,
    MODE_TREE,
    MODE_SORT,
    MODE_FILTER,
    MODE_SEARCH,
    MODE_CONFIRM,
    MODE_CONTROL
} UIMode;

typedef struct {
    UIMode mode;
    int cursor;      /* index into the filtered view  */
    int scroll;      /* first visible row             */
    int body_rows;   /* visible table rows (viewport height) */
    int selected_pid;
    SortKey sort_key;
    int sort_desc;
    char search[MAX_SEARCH];
    char search_buf[MAX_SEARCH];
    char filter_state; /* 'A' = all */
    char msg[192];
    int confirm_action; /* CtrlAction pending confirmation, -1 none */
    int *view;          /* indices into vec->items */
    size_t view_cap;
    size_t view_count;
} UIState;

static const char *sort_key_name(SortKey k)
{
    switch (k) {
    case SORT_PID:   return "PID";
    case SORT_NAME:  return "Process";
    case SORT_CPU:   return "CPU%";
    case SORT_MEM:   return "Memory";
    case SORT_STATE: return "State";
    case SORT_USER:  return "User";
    }
    return "?";
}

static void ui_msg(UIState *ui, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui->msg, sizeof ui->msg, fmt, ap);
    va_end(ap);
}

static void rebuild_view(const ProcVec *vec, UIState *ui)
{
    size_t i;
    if (vec->count > ui->view_cap) {
        ui->view_cap = vec->count + 64;
        ui->view = xrealloc(ui->view, ui->view_cap * sizeof *ui->view);
    }
    ui->view_count = 0;
    for (i = 0; i < vec->count; ++i) {
        const ProcessInfo *pi = &vec->items[i];
        if (!process_matches_state(pi, ui->filter_state))
            continue;
        if (ui->search[0] != '\0' && !process_matches(pi, ui->search))
            continue;
        ui->view[ui->view_count++] = (int)i;
    }
}

static void clamp_cursor(UIState *ui, int count)
{
    int body = ui->body_rows > 0 ? ui->body_rows : 1;
    if (count <= 0) {
        ui->cursor = 0;
        ui->scroll = 0;
        return;
    }
    if (ui->cursor >= count)
        ui->cursor = count - 1;
    if (ui->cursor < 0)
        ui->cursor = 0;
    /* viewport scrolling: only move the window when the cursor leaves it */
    if (ui->cursor < ui->scroll)
        ui->scroll = ui->cursor;
    if (ui->cursor >= ui->scroll + body)
        ui->scroll = ui->cursor - body + 1;
}

/* keep the selected PID sticky across refreshes */
static void keep_selection(const ProcVec *vec, const UIState *ui,
                           int *cursor)
{
    size_t i;
    if (ui->view_count == 0)
        return;
    for (i = 0; i < ui->view_count; ++i)
        if (vec->items[ui->view[i]].pid == ui->selected_pid) {
            *cursor = (int)i;
            return;
        }
}

/* the cursor IS the selection: keep selected_pid in sync so the
 * 1 s auto-refresh does not snap the highlight back to an older row */
static const ProcessInfo *selected_proc(const ProcVec *vec,
                                        const UIState *ui);

static void sync_selected(const ProcVec *vec, UIState *ui)
{
    const ProcessInfo *sel = selected_proc(vec, ui);
    if (sel)
        ui->selected_pid = sel->pid;
}

static const ProcessInfo *selected_proc(const ProcVec *vec,
                                        const UIState *ui)
{
    if (ui->cursor < 0 || ui->cursor >= (int)ui->view_count)
        return NULL;
    return &vec->items[ui->view[ui->cursor]];
}

/* ------------------------------------------------------------------ */
/* Drawing: shared chrome                                              */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Drawing: main table                                                 */
/* ------------------------------------------------------------------ */
static void fb_rule(FrameBuf *fb, int row, int width, const char *style)
{
    int i;
    const char *dash = bx("\xE2\x94\x80", "-");
    fb_fmt(fb, "\x1b[%d;1H%s", row, style);
    for (i = 0; i < width; ++i)
        fb_raw(fb, dash);
    fb_raw(fb, A_RESET);
}

static void fb_blank_row(FrameBuf *fb, int row, int width)
{
    int i;
    fb_fmt(fb, "\x1b[%d;1H", row);
    for (i = 0; i < width; ++i)
        fb_raw(fb, " ");
}

static void fb_center_text(FrameBuf *fb, int row, int width,
                           const char *style, const char *text)
{
    int tw = (int)utf8_width(text);
    int pad = (width - tw) / 2;
    int i;
    if (pad < 0)
        pad = 0;
    fb_fmt(fb, "\x1b[%d;1H", row);
    for (i = 0; i < pad; ++i)
        fb_raw(fb, " ");
    fb_raw(fb, style);
    fb_raw(fb, text);
    fb_raw(fb, A_RESET);
}

/* ------------------------------------------------------------------ */
/* Drawing: main table                                                 */
/* ------------------------------------------------------------------ */

static void draw_frame_table(UIState *ui, FrameBuf *fb, const ProcVec *vec,
                             const SystemStats *stats, int w, int h)
{
    char upbuf[64];
    LineWriter lw;
    int body_top = 7;
    int body_bot = h - 4; /* inclusive rows for the table body */
    int body_rows = body_bot - body_top + 1;
    size_t n_r = 0, n_s = 0, n_d = 0, n_z = 0, i;

    if (body_rows < 1)
        body_rows = 1;

    for (i = 0; i < vec->count; ++i) {
        switch (vec->items[i].state) {
        case 'R': ++n_r; break;
        case 'S': ++n_s; break;
        case 'D': ++n_d; break;
        case 'Z': ++n_z; break;
        default: break;
        }
    }

    fb_blank_row(fb, 1, w);
    fb_center_text(fb, 1, w, A_CYAN A_BOLD,
                   "LINUX PROCESS MONITORING & CONTROL SYSTEM");
    fb_rule(fb, 2, w, A_GREY);

    fmt_uptime(stats->uptime_seconds, upbuf, sizeof upbuf);
    lw_begin(&lw, fb, 3, w);
    lw_textf(&lw, A_BOLD, " Hostname : %s", stats->hostname);
    lw_textf(&lw, "", "    Kernel : %s", stats->kernel);
    lw_textf(&lw, "", "    Uptime : %s", upbuf);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 4, w);
    lw_textf(&lw, A_BOLD, " CPU : %.1f%%", stats->cpu_percent);
    lw_textf(&lw, "", "   Memory : %.1f%% (%.0f/%.0f MB)",
             stats->mem_used_percent, stats->mem_used_mb,
             stats->mem_total_kb / 1024.0);
    lw_textf(&lw, "", "   Processes : %zu  (R:%zu S:%zu D:%zu Z:%zu)",
             stats->process_count, n_r, n_s, n_d, n_z);
    lw_pad(&lw, " ");
    fb_rule(fb, 5, w, A_GREY);

    /* header — same column grid as the rows below */
    lw_begin(&lw, fb, 6, w);
    lw_text(&lw, A_CYAN A_BOLD, " ");            /* row marker        */
    lw_pad_to(&lw, 8, A_CYAN A_BOLD);            /* PID  cols 2-8     */
    lw_text(&lw, A_CYAN A_BOLD, "PROCESS");
    lw_pad_to(&lw, 31, A_CYAN A_BOLD);           /* NAME cols 9-30    */
    lw_text(&lw, A_CYAN A_BOLD, "USER");         /* USER cols 31-41   */
    lw_pad_to(&lw, 42, A_CYAN A_BOLD);
    lw_text(&lw, A_CYAN A_BOLD, "CPU%");
    lw_pad_to(&lw, 48, A_CYAN A_BOLD);           /* CPU  cols 42-47   */
    lw_text(&lw, A_CYAN A_BOLD, "MEM%");
    lw_pad_to(&lw, 54, A_CYAN A_BOLD);           /* MEM  cols 48-53   */
    lw_text(&lw, A_CYAN A_BOLD, "STATE");
    lw_pad_to(&lw, 68, A_CYAN A_BOLD);           /* STATE cols 54-67  */
    lw_text(&lw, A_CYAN A_BOLD, "PPID");
    lw_pad(&lw, " ");

    /* scrolling window */
    if (ui->scroll > ui->cursor)
        ui->scroll = ui->cursor;
    if (ui->cursor >= ui->scroll + body_rows)
        ui->scroll = ui->cursor - body_rows + 1;
    if (ui->scroll < 0)
        ui->scroll = 0;

    {
        int r;
        for (r = 0; r < body_rows; ++r) {
            int idx = ui->scroll + r;
            int row = body_top + r;
            if (idx >= (int)ui->view_count) {
                if (ui->view_count == 0 && r == 0) {
                    lw_begin(&lw, fb, row, w);
                    lw_text(&lw, A_YELL,
                            "  No processes match the current search/filter.");
                    lw_pad(&lw, " ");
                } else {
                    fb_blank_row(fb, row, w);
                }
                continue;
            }
            {
                const ProcessInfo *pi = &vec->items[ui->view[idx]];
                int sel = (idx == ui->cursor);
                const char *base = sel ? A_REV A_BOLD : "";
                const char *sc = state_color(pi->state);
                char num[32];

                lw_begin(&lw, fb, row, w);
                lw_text(&lw, sel ? A_REV A_BOLD : A_DIM, " ");
                snprintf(num, sizeof num, "%6d ", pi->pid);
                lw_text(&lw, base, num);                 /* -> col 8  */
                {
                    char cell[128];
                    utf8_copy(cell, sizeof cell, pi->name, 20);
                    lw_text(&lw, base, cell);
                }
                lw_pad_to(&lw, 30, base);                /* -> col 30 */
                {
                    char cell[64];
                    utf8_copy(cell, sizeof cell,
                              pi->user[0] ? pi->user : "-", 10);
                    lw_text(&lw, base, cell);
                }
                lw_pad_to(&lw, 41, base);                /* -> col 41 */
                snprintf(num, sizeof num, "%6.1f ", pi->cpu_percent);
                lw_text(&lw, sel ? base
                                 : (pi->cpu_percent > 40.0 ? A_YELL : ""),
                        num);                                 /* -> 47   */
                snprintf(num, sizeof num, "%6.1f ", pi->memory_percent);
                lw_text(&lw, base, num);                 /* -> col 53 */
                lw_pad_to(&lw, 54, base);
                {
                    char cell[32];
                    utf8_copy(cell, sizeof cell, pi->state_name, 13);
                    lw_text(&lw, sel ? base : sc, cell);
                }
                lw_pad_to(&lw, 68, base);
                snprintf(num, sizeof num, "%5d ", pi->ppid);
                lw_text(&lw, base, num);
                lw_pad(&lw, sel ? A_REV : "");
            }
        }
    }

    fb_rule(fb, h - 3, w, A_GREY);
    lw_begin(&lw, fb, h - 2, w);
    lw_textf(&lw, A_CYAN, " Sort: %s%s", sort_key_name(ui->sort_key),
             ui->sort_desc ? " desc" : " asc");
    lw_textf(&lw, "", "   Filter: %s",
             ui->filter_state == 'A' ? "all" : (char[2]){ui->filter_state, 0});
    lw_textf(&lw, "", "   Search: %s",
             ui->search[0] ? ui->search : "-");
    if (ui->msg[0]) {
        lw_text(&lw, A_YELL A_BOLD, "   ");
        lw_textf(&lw, A_YELL A_BOLD, "%s", ui->msg);
    }
    lw_pad(&lw, " ");

    lw_begin(&lw, fb, h - 1, w);
    lw_text(&lw, A_GREY,
            " [Up/Down] Select  [Enter] Details  [R] Refresh  [S] Sort  "
            "[F] Filter");
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, h, w);
    lw_text(&lw, A_GREY,
            " [/] Search  [T] Tree  [C] Control  [D] OS Demo  [Q] Quit");
    lw_pad(&lw, " ");
}

/* ------------------------------------------------------------------ */
/* Popups                                                              */
/* ------------------------------------------------------------------ */

static void draw_boxed_menu(FrameBuf *fb, int w, int h, const char *title,
                            const char *const *lines, int nlines)
{
    int bw = 46;
    int bh = nlines + 2;
    int top = (h - bh) / 2;
    int left = (w - bw) / 2;
    int i;
    if (top < 1)
        top = 1;
    if (left < 0)
        left = 0;

    fb_fmt(fb, "\x1b[%d;%dH%s\xC9\xCD\xCD", top, left, A_CYAN A_BOLD);
    /* top border */
    fb_fmt(fb, "\x1b[%d;%dH%s", top, left, A_CYAN A_BOLD);
    fb_raw(fb, bx("\xE2\x94\x8C", "+"));
    for (i = 0; i < bw - 2; ++i)
        fb_raw(fb, bx("\xE2\x94\x80", "-"));
    fb_raw(fb, bx("\xE2\x94\x90", "+"));
    fb_raw(fb, A_RESET);

    fb_fmt(fb, "\x1b[%d;%dH%s %s ", top, left + 2, A_RESET A_BOLD, title);

    for (i = 0; i < nlines; ++i) {
        int row = top + 1 + i;
        int j;
        fb_fmt(fb, "\x1b[%d;%dH%s", row, left, A_CYAN);
        fb_raw(fb, bx("\xE2\x94\x82", "|"));
        fb_raw(fb, A_RESET);
        fb_raw(fb, " ");
        fb_raw(fb, lines[i]);
        {
            int used = 2 + (int)utf8_width(lines[i]);
            for (j = used; j < bw - 1; ++j)
                fb_raw(fb, " ");
        }
        fb_fmt(fb, "%s", A_CYAN);
        fb_raw(fb, bx("\xE2\x94\x82", "|"));
        fb_raw(fb, A_RESET);
    }

    /* bottom border */
    fb_fmt(fb, "\x1b[%d;%dH%s", top + bh, left, A_CYAN);
    fb_raw(fb, bx("\xE2\x94\x94", "+"));
    for (i = 0; i < bw - 2; ++i)
        fb_raw(fb, bx("\xE2\x94\x80", "-"));
    fb_raw(fb, bx("\xE2\x94\x98", "+"));
    fb_raw(fb, A_RESET);
}

static void draw_popups(UIState *ui, FrameBuf *fb, int w, int h)
{
    if (ui->mode == MODE_SORT) {
        static const char *lines[] = {
            " [1] PID            [2] Process name",
            " [3] CPU percent    [4] Memory",
            " [5] State          [6] User",
            "",
            " Pick a key; picking the current key",
            " toggles ascending / descending. Esc cancels."
        };
        draw_boxed_menu(fb, w, h, "SORT BY", lines, 6);
    } else if (ui->mode == MODE_FILTER) {
        static const char *lines[] = {
            " [A] All states",
            " [R] Running      [S] Sleeping",
            " [D] Disk sleep   [T] Stopped",
            " [Z] Zombie       [I] Idle",
            "",
            " Esc cancels."
        };
        draw_boxed_menu(fb, w, h, "FILTER BY STATE", lines, 6);
    } else if (ui->mode == MODE_CONTROL) {
        static const char *lines[] = {
            " [1] Pause   (SIGSTOP)",
            " [2] Resume  (SIGCONT)",
            " [3] Stop    (SIGTERM)",
            " [4] Kill    (SIGKILL - confirmation required)",
            "",
            " Esc cancels."
        };
        draw_boxed_menu(fb, w, h, "PROCESS CONTROL", lines, 6);
    } else if (ui->mode == MODE_CONFIRM) {
        const char *lines[] = {
            "",
            " This action cannot be undone.",
            "",
            " Continue? [y/N]",
            ""
        };
        draw_boxed_menu(fb, w, h, "WARNING: FORCE TERMINATION", lines, 5);
    } else if (ui->mode == MODE_SEARCH) {
        LineWriter lw;
        lw_begin(&lw, fb, h, w);
        lw_textf(&lw, A_YELL A_BOLD, " Search: %s", ui->search_buf);
        lw_pad(&lw, A_REV);
        write_str("\x1b[?25h"); /* show cursor while typing */
    }
}

/* ------------------------------------------------------------------ */
/* Drawing: details view                                               */
/* ------------------------------------------------------------------ */

static void draw_frame_details(UIState *ui, FrameBuf *fb,
                               const ProcessInfo *pi, int w, int h)
{
    LineWriter lw;
    char mb[64];
    char line2[256];

    fb_blank_row(fb, 1, w);
    fb_center_text(fb, 1, w, A_CYAN A_BOLD, "PROCESS DETAILS");
    fb_rule(fb, 2, w, A_GREY);

    lw_begin(&lw, fb, 3, w);
    lw_textf(&lw, A_BOLD, " Process       : %s", pi->name);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 4, w);
    lw_textf(&lw, "", " PID           : %d", pi->pid);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 5, w);
    lw_textf(&lw, "", " Parent PID    : %d", pi->ppid);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 6, w);
    lw_textf(&lw, "", " User          : %s (uid %u)",
             pi->user[0] ? pi->user : "-", (unsigned)pi->uid);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 7, w);
    snprintf(line2, sizeof line2, "%s (%c)", pi->state_name, pi->state);
    lw_textf(&lw, state_color(pi->state), " State         : %s", line2);
    lw_pad(&lw, " ");
    fb_rule(fb, 8, w, A_GREY);

    lw_begin(&lw, fb, 9, w);
    lw_textf(&lw, A_BOLD, " CPU Usage     : %.1f%% (interval)",
             pi->cpu_percent);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 10, w);
    snprintf(mb, sizeof mb, "%.1f MB (%.1f%% of RAM)", pi->memory_mb,
             pi->memory_percent);
    lw_textf(&lw, "", " Memory        : %s", mb);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 11, w);
    lw_textf(&lw, "", " RSS           : %lu pages", pi->rss_pages);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 12, w);
    lw_textf(&lw, "", " Priority      : %ld      Nice: %ld", pi->priority,
             pi->nice);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 13, w);
    lw_textf(&lw, "", " Threads       : %lu", pi->threads);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 14, w);
    lw_textf(&lw, "", " Ticks (u+s)   : %llu", pi->total_ticks);
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 15, w);
    lw_textf(&lw, "", " Executable    : %s",
             pi->exe[0] ? pi->exe : "-");
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, 16, w);
    lw_textf(&lw, "", " Command line  : %s", pi->cmdline);
    lw_pad(&lw, " ");

    fb_rule(fb, h - 3, w, A_GREY);
    lw_begin(&lw, fb, h - 2, w);
    if (ui->msg[0])
        lw_textf(&lw, A_YELL A_BOLD, " %s", ui->msg);
    else
        lw_text(&lw, A_GREY, " ");
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, h - 1, w);
    lw_text(&lw, A_GREY,
            " [P] Pause   [R] Resume   [S] Stop (SIGTERM)");
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, h, w);
    lw_text(&lw, A_GREY,
            " [K] Kill (SIGKILL, confirm)   [B/Esc] Back");
    lw_pad(&lw, " ");
}

/* ------------------------------------------------------------------ */
/* Drawing: tree view                                                  */
/* ------------------------------------------------------------------ */

static void draw_frame_tree(UIState *ui, FrameBuf *fb, TreeLines *tl,
                            int w, int h)
{
    LineWriter lw;
    int body_top = 4;
    int body_bot = h - 3;
    int body_rows = body_bot - body_top + 1;
    int r;

    fb_blank_row(fb, 1, w);
    fb_center_text(fb, 1, w, A_CYAN A_BOLD, "PROCESS TREE (PID / PPID hierarchy)");
    fb_rule(fb, 2, w, A_GREY);
    lw_begin(&lw, fb, 3, w);
    lw_textf(&lw, A_GREY, " %zu processes, roots and orphans listed first",
             tl->count);
    lw_pad(&lw, " ");

    if (ui->scroll > ui->cursor)
        ui->scroll = ui->cursor;
    if (ui->cursor >= ui->scroll + body_rows)
        ui->scroll = ui->cursor - body_rows + 1;
    if (ui->scroll < 0)
        ui->scroll = 0;

    for (r = 0; r < body_rows; ++r) {
        int idx = ui->scroll + r;
        int row = body_top + r;
        if (idx >= (int)tl->count) {
            fb_blank_row(fb, row, w);
            continue;
        }
        {
            const TreeLine *line = &tl->items[idx];
            int sel = (idx == ui->cursor && line->pid != 0);
            lw_begin(&lw, fb, row, w);
            lw_text(&lw, sel ? A_REV : (line->pid == 0 ? A_GREY : ""),
                    line->text);
            lw_pad(&lw, sel ? A_REV : "");
        }
    }
    fb_rule(fb, h - 2, w, A_GREY);
    lw_begin(&lw, fb, h - 1, w);
    lw_text(&lw, A_GREY,
            " [Up/Down] Move   [Enter] Details   [T/B/Esc] Back   [Q] Quit");
    lw_pad(&lw, " ");
    lw_begin(&lw, fb, h, w);
    lw_pad(&lw, " ");
}

/* ------------------------------------------------------------------ */
/* Actions: process control                                            */
/* ------------------------------------------------------------------ */

static void do_control(MonitorState *mon, ProcVec *vec, SystemStats *stats,
                       UIState *ui, int pid, unsigned long long starttime,
                       const char *pname,
                       CtrlAction action)
{
    char err[192];
    char msg[192];
    ProcessInfo check;

    if (ctrl_validate(pid, action, err, sizeof err) != 0) {
        ui_msg(ui, "%s", err);
        return;
    }
    if (ctrl_send_verified(pid, starttime, action) != 0) {
        ctrl_describe_failure(pid, action, msg, sizeof msg);
        ui_msg(ui, "%s", msg);
        return;
    }
    /* Post-signal verification: re-read the process. */
    if (get_process_info(pid, &check) != 0) {
        ui_msg(ui, "Process %d (%s) is no longer running.", pid,
               pname ? pname : "?");
    } else if (action == CTRL_PAUSE && check.state == 'T') {
        ui_msg(ui, "Process %d (%s) paused - state STOPPED (T)", pid,
               check.name);
    } else if (action == CTRL_RESUME && check.state != 'T') {
        ui_msg(ui, "Process %d (%s) resumed - state %s (%c)", pid,
               check.name, check.state_name, check.state);
    } else {
        ui_msg(ui, "%s sent to %d (%s) - state now %s (%c)",
               ctrl_signal_name(action), pid, check.name, check.state_name,
               check.state);
    }
    monitor_refresh(mon, vec, stats); /* spec: refresh after every signal */
    ui->mode = MODE_TABLE;
}

/* ------------------------------------------------------------------ */
/* Main interactive loop                                               */
/* ------------------------------------------------------------------ */

void ui_run(MonitorState *mon, ProcVec *vec, SystemStats *stats)
{
    UIState ui;
    FrameBuf fb;
    TreeLines tree;
    ProcessInfo details_info;
    int details_pid = -1;
    int have_tree = 0;
    int w = 80, h = 24;

    memset(&ui, 0, sizeof ui);
    memset(&tree, 0, sizeof tree);
    ui.mode = MODE_TABLE;
    ui.sort_key = SORT_PID;
    ui.filter_state = 'A';
    ui.confirm_action = -1;

    if (terminal_enter() != 0) {
        fprintf(stderr,
                "error: this program needs an interactive terminal\n");
        return;
    }
    install_signal_handlers();
    fb_init(&fb);

    {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col &&
            ws.ws_row) {
            w = ws.ws_col;
            h = ws.ws_row;
        }
    }

    monitor_refresh(mon, vec, stats);
    rebuild_view(vec, &ui);
    if (ui.view_count > 0) {
        ui.selected_pid = vec->items[ui.view[0]].pid;
        ui.cursor = 0;
    }

    while (!g_stop) {
        int timeout = UI_REFRESH_MS;
        Key key;
        int must_refresh = 0;

        /* terminal size can change at any moment */
        {
            struct winsize ws;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col &&
                ws.ws_row) {
                w = ws.ws_col;
                h = ws.ws_row;
            }
        }

        /* viewport height of the table body (chrome rows around it) */
        ui.body_rows = h - 10 > 1 ? h - 10 : 1;

        /* auto refresh (also refreshes the details screen) */
        monitor_refresh(mon, vec, stats);
        rebuild_view(vec, &ui);
        keep_selection(vec, &ui, &ui.cursor);
        clamp_cursor(&ui, (int)ui.view_count);
        if (ui.view_count > 0 && ui.selected_pid == 0) {
            ui.selected_pid = vec->items[ui.view[ui.cursor]].pid;
        }

        if (ui.mode == MODE_DETAILS && details_pid > 0) {
            if (monitor_sample_details(mon, details_pid, &details_info) !=
                0) {
                ui_msg(&ui, "Process %d is no longer running.",
                       details_pid);
                ui.mode = MODE_TABLE;
            }
        }

        if (w < UI_MIN_COLS || h < UI_MIN_ROWS) {
            fb.len = 0;
            fb_raw(&fb, "\x1b[H\x1b[2J");
            fb_center_text(&fb, 1, w, A_YELL A_BOLD,
                           "Terminal too small - need at least 80x24");
            fb_flush(&fb);
            fb_invalidate();
            read_key(UI_REFRESH_MS, &key);
            continue;
        }

        /* draw current frame */
        fb.len = 0;
        fb_raw(&fb, "\x1b[H");
        if (ui.mode == MODE_DETAILS)
            draw_frame_details(&ui, &fb, &details_info, w, h);
        else if (ui.mode == MODE_TREE) {
            if (!have_tree) {
                tree_build(vec, &tree);
                have_tree = 1;
            }
            draw_frame_tree(&ui, &fb, &tree, w, h);
        } else
            draw_frame_table(&ui, &fb, vec, stats, w, h);
        if (ui.mode != MODE_SEARCH)
            draw_popups(&ui, &fb, w, h);
        fb_raw(&fb, A_RESET);
        /* repaint only changed rows after the first full frame: this is
         * what kills the conpty/Windows Terminal flicker */
        if (g_prev.valid)
            fb_flush_diff(&fb, w, h);
        else
            fb_flush(&fb);

        /* input with the remaining budget of this refresh window */
        if (read_key(timeout, &key) <= 0) {
            if (g_stop)
                break;
            continue;
        }

        ui.msg[0] = '\0';
        write_str("\x1b[?25l");

        switch (ui.mode) {
        case MODE_TABLE: {
            const ProcessInfo *sel = selected_proc(vec, &ui);
            switch (key.kind) {
            case K_UP: --ui.cursor; clamp_cursor(&ui, (int)ui.view_count);
                       sync_selected(vec, &ui); break;
            case K_DOWN: ++ui.cursor; clamp_cursor(&ui, (int)ui.view_count);
                         sync_selected(vec, &ui); break;
            case K_PGUP: ui.cursor -= 10;
                         clamp_cursor(&ui, (int)ui.view_count);
                         sync_selected(vec, &ui); break;
            case K_PGDN: ui.cursor += 10;
                         clamp_cursor(&ui, (int)ui.view_count);
                         sync_selected(vec, &ui); break;
            case K_HOME: ui.cursor = 0; clamp_cursor(&ui, (int)ui.view_count);
                         sync_selected(vec, &ui); break;
            case K_END: ui.cursor = (int)ui.view_count - 1;
                        clamp_cursor(&ui, (int)ui.view_count);
                        sync_selected(vec, &ui); break;
            case K_ENTER:
                if (sel) {
                    ui.selected_pid = sel->pid;
                    details_pid = sel->pid;
                    ui.mode = MODE_DETAILS;
                }
                break;
            case K_ESC: break;
            case K_CHAR:
                switch (key.ch) {
                case 'q': case 'Q': g_stop = 1; break;
                case 'r': case 'R': must_refresh = 1; break;
                case 's': case 'S': ui.mode = MODE_SORT; break;
                case 'f': case 'F': ui.mode = MODE_FILTER; break;
                case 't': case 'T': ui.mode = MODE_TREE; have_tree = 0;
                                    ui.cursor = 0; ui.scroll = 0; break;
                case 'c': case 'C':
                    if (sel) { ui.selected_pid = sel->pid;
                               ui.mode = MODE_CONTROL; }
                    break;
                case '/':
                    ui.mode = MODE_SEARCH;
                    sstrcpy(ui.search_buf, sizeof ui.search_buf,
                            ui.search);
                    break;
                case 'd': case 'D':
                    terminal_leave_alt();
                    printf("\x1b[2J\x1b[H");
                    run_process_demo();
                    printf("\nPress Enter to return to the monitor...");
                    fflush(stdout);
                    {
                        Key k;
                        while (read_key(-1, &k) >= 0 &&
                               k.kind != K_ENTER && !g_stop)
                            ;
                    }
                    write_str("\x1b[?1049h\x1b[?25l");
                    fb_invalidate(); /* overlay replaced the alt screen */
                    must_refresh = 1;
                    break;
                default: break;
                }
                break;
            default: break;
            }
            if (must_refresh) {
                monitor_refresh(mon, vec, stats);
                rebuild_view(vec, &ui);
                keep_selection(vec, &ui, &ui.cursor);
                clamp_cursor(&ui, (int)ui.view_count);
            }
            break;
        }

        case MODE_DETAILS:
            switch (key.kind) {
            case K_ESC: ui.mode = MODE_TABLE; break;
            case K_CHAR:
                switch (key.ch) {
                case 'b': case 'B': ui.mode = MODE_TABLE; break;
                case 'p': case 'P':
                    do_control(mon, vec, stats, &ui, details_pid,
                               details_info.starttime, details_info.name,
                               CTRL_PAUSE);
                    break;
                case 'r': case 'R':
                    do_control(mon, vec, stats, &ui, details_pid,
                               details_info.starttime, details_info.name,
                               CTRL_RESUME);
                    break;
                case 's': case 'S':
                    do_control(mon, vec, stats, &ui, details_pid,
                               details_info.starttime, details_info.name,
                               CTRL_STOP);
                    break;
                case 'k': case 'K':
                    ui.confirm_action = CTRL_KILL;
                    ui.mode = MODE_CONFIRM;
                    break;
                default: break;
                }
                break;
            default: break;
            }
            break;

        case MODE_TREE:
            switch (key.kind) {
            case K_UP: --ui.cursor; clamp_cursor(&ui, (int)tree.count); break;
            case K_DOWN: ++ui.cursor; clamp_cursor(&ui, (int)tree.count); break;
            case K_PGUP: ui.cursor -= 10; clamp_cursor(&ui, (int)tree.count);
                         break;
            case K_PGDN: ui.cursor += 10; clamp_cursor(&ui, (int)tree.count);
                         break;
            case K_HOME: ui.cursor = 0; clamp_cursor(&ui, (int)tree.count);
                         break;
            case K_END: ui.cursor = (int)tree.count - 1;
                        clamp_cursor(&ui, (int)tree.count); break;
            case K_ENTER: {
                if (ui.cursor >= 0 && ui.cursor < (int)tree.count &&
                    tree.items[ui.cursor].pid > 0) {
                    details_pid = tree.items[ui.cursor].pid;
                    ui.mode = MODE_DETAILS;
                }
                break;
            }
            case K_ESC: ui.mode = MODE_TABLE; break;
            case K_CHAR:
                if (key.ch == 'q' || key.ch == 'Q')
                    g_stop = 1;
                else if (key.ch == 't' || key.ch == 'T' ||
                         key.ch == 'b' || key.ch == 'B')
                    ui.mode = MODE_TABLE;
                break;
            default: break;
            }
            break;

        case MODE_SORT:
            if (key.kind == K_ESC) {
                ui.mode = MODE_TABLE;
            } else if (key.kind == K_CHAR &&
                       key.ch >= '1' && key.ch <= '6') {
                SortKey k = (SortKey)(key.ch - '1');
                if (k == ui.sort_key)
                    ui.sort_desc = !ui.sort_desc;
                else {
                    ui.sort_key = k;
                    ui.sort_desc = (k == SORT_CPU || k == SORT_MEM);
                }
                sort_processes((ProcVec *)vec, ui.sort_key,
                               (bool)ui.sort_desc);
                rebuild_view(vec, &ui);
                keep_selection(vec, &ui, &ui.cursor);
                clamp_cursor(&ui, (int)ui.view_count);
                ui.mode = MODE_TABLE;
            }
            break;

        case MODE_FILTER:
            if (key.kind == K_ESC) {
                ui.mode = MODE_TABLE;
            } else if (key.kind == K_CHAR) {
                char c = (char)toupper((unsigned char)key.ch);
                if (c == 'A' || c == 'R' || c == 'S' || c == 'D' ||
                    c == 'T' || c == 'Z' || c == 'I') {
                    ui.filter_state = c;
                    rebuild_view(vec, &ui);
                    keep_selection(vec, &ui, &ui.cursor);
                    clamp_cursor(&ui, (int)ui.view_count);
                    ui.mode = MODE_TABLE;
                }
            }
            break;

        case MODE_CONTROL:
            if (key.kind == K_ESC) {
                ui.mode = MODE_TABLE;
            } else if (key.kind == K_CHAR) {
                const ProcessInfo *sel = NULL;
                size_t i;
                for (i = 0; i < ui.view_count; ++i)
                    if (vec->items[ui.view[i]].pid == ui.selected_pid)
                        sel = &vec->items[ui.view[i]];
                if (!sel)
                    ui.mode = MODE_TABLE;
                else if (key.ch == '1')
                    do_control(mon, vec, stats, &ui, sel->pid,
                               sel->starttime, sel->name, CTRL_PAUSE);
                else if (key.ch == '2')
                    do_control(mon, vec, stats, &ui, sel->pid,
                               sel->starttime, sel->name, CTRL_RESUME);
                else if (key.ch == '3')
                    do_control(mon, vec, stats, &ui, sel->pid,
                               sel->starttime, sel->name, CTRL_STOP);
                else if (key.ch == '4') {
                    ui.confirm_action = CTRL_KILL;
                    ui.mode = MODE_CONFIRM;
                }
            }
            break;

        case MODE_CONFIRM:
            if (key.kind == K_CHAR &&
                (key.ch == 'y' || key.ch == 'Y')) {
                CtrlAction a = (CtrlAction)ui.confirm_action;
                const ProcessInfo *sel = NULL;
                size_t i;
                for (i = 0; i < ui.view_count; ++i)
                    if (vec->items[ui.view[i]].pid == ui.selected_pid)
                        sel = &vec->items[ui.view[i]];
                ui.confirm_action = -1;
                ui.mode = MODE_TABLE;
                if (sel)
                    do_control(mon, vec, stats, &ui, sel->pid,
                               sel->starttime, sel->name, a);
            } else if (key.kind == K_ESC ||
                       (key.kind == K_CHAR &&
                        (key.ch == 'n' || key.ch == 'N')) ||
                       key.kind == K_ENTER) {
                ui.confirm_action = -1;
                ui.mode = MODE_TABLE;
            }
            break;

        case MODE_SEARCH:
            switch (key.kind) {
            case K_ESC:
                ui.mode = MODE_TABLE;
                break;
            case K_ENTER:
                sstrcpy(ui.search, sizeof ui.search, ui.search_buf);
                ui.mode = MODE_TABLE;
                rebuild_view(vec, &ui);
                keep_selection(vec, &ui, &ui.cursor);
                clamp_cursor(&ui, (int)ui.view_count);
                break;
            case K_BACKSPACE: {
                size_t l = strlen(ui.search_buf);
                if (l > 0) {
                    /* back off one UTF-8 code point */
                    while (l > 1 &&
                           ((unsigned char)ui.search_buf[l - 1] & 0xC0)
                               == 0x80)
                        --l;
                    ui.search_buf[l - 1] = '\0';
                }
                break;
            }
            case K_CHAR: {
                size_t l = strlen(ui.search_buf);
                if (l + 1 < sizeof ui.search_buf) {
                    ui.search_buf[l] = key.ch;
                    ui.search_buf[l + 1] = '\0';
                }
                break;
            }
            default: break;
            }
            break;
        }
    }

    terminal_restore();
    prev_cache_free();
    fb_free(&fb);
    tree_lines_free(&tree);
    free(ui.view);
}

/* ------------------------------------------------------------------ */
/* Batch (non-interactive) output — evidence mode for docs & testing   */
/* ------------------------------------------------------------------ */

void ui_print_batch_table(const ProcVec *vec, const SystemStats *stats)
{
    char upbuf[64];
    size_t i;
    size_t n_r = 0, n_s = 0, n_d = 0, n_z = 0;

    fmt_uptime(stats->uptime_seconds, upbuf, sizeof upbuf);
    for (i = 0; i < vec->count; ++i) {
        switch (vec->items[i].state) {
        case 'R': ++n_r; break;
        case 'S': ++n_s; break;
        case 'D': ++n_d; break;
        case 'Z': ++n_z; break;
        default: break;
        }
    }

    printf("===============================================================\n");
    printf("        LINUX PROCESS MONITORING & CONTROL SYSTEM\n");
    printf("===============================================================\n");
    printf(" Hostname : %s   Kernel : %s\n", stats->hostname, stats->kernel);
    printf(" Uptime   : %s   CPUs   : %ld\n", upbuf, stats->cpu_count);
    printf(" CPU      : %.1f%%   Memory : %.1f%% (%.0f/%.0f MB)\n",
           stats->cpu_percent, stats->mem_used_percent, stats->mem_used_mb,
           stats->mem_total_kb / 1024.0);
    printf(" Processes: %zu  (R:%zu S:%zu D:%zu Z:%zu)\n", vec->count,
           n_r, n_s, n_d, n_z);
    printf("---------------------------------------------------------------\n");
    printf("%7s  %-20s %-10s %6s %6s  %-13s %6s\n", "PID", "PROCESS",
           "USER", "CPU%", "MEM%", "STATE", "PPID");
    printf("---------------------------------------------------------------\n");
    for (i = 0; i < vec->count; ++i) {
        const ProcessInfo *pi = &vec->items[i];
        char name[24], user[16];
        utf8_copy(name, sizeof name, pi->name, 20);
        utf8_copy(user, sizeof user, pi->user[0] ? pi->user : "-", 10);
        printf("%7d  %-20s %-10s %6.1f %6.1f  %-13s %6d\n", pi->pid,
               name, user, pi->cpu_percent, pi->memory_percent,
               pi->state_name, pi->ppid);
    }
    printf("---------------------------------------------------------------\n");
    printf("%zu processes listed. All values read live from /proc.\n",
           vec->count);
}

void ui_print_batch_tree(ProcVec *vec, int utf8)
{
    TreeLines tl;
    size_t i;

    ui_set_utf8(utf8);
    if (tree_build(vec, &tl) != 0) {
        printf("error: cannot build the process tree\n");
        return;
    }
    printf("================ PROCESS TREE (PID/PPID) =================\n");
    for (i = 0; i < tl.count; ++i) {
        if (tl.items[i].pid > 0)
            printf("%s\n", tl.items[i].text);
    }
    printf("==========================================================\n");
    tree_lines_free(&tl);
}
