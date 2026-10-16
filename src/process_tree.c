/*
 * process_tree.c — PID/PPID hierarchy builder and renderer.
 *
 * The tree is computed from the actual snapshot relationships; nothing is
 * shelled out to pstree. Rendering uses Unicode branch art when the locale
 * is UTF-8 and ASCII fallbacks otherwise (decided by the UI).
 */
#include "process_tree.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* children index: for each position in vec, a growable list of positions */
typedef struct {
    size_t *items;
    size_t count, cap;
} PosList;

static void poslist_push(PosList *pl, size_t pos)
{
    if (pl->count == pl->cap) {
        pl->cap = pl->cap ? pl->cap * 2 : 4;
        pl->items = xrealloc(pl->items, pl->cap * sizeof *pl->items);
    }
    pl->items[pl->count++] = pos;
}

static int pos_is_less(const void *a, const void *b);

static void sort_children_by_pid(PosList *pl, const ProcVec *v)
{
    size_t i, j;
    /* insertion sort on PID: lists are tiny and this avoids qsort_r */
    for (i = 1; i < pl->count; ++i) {
        size_t key = pl->items[i];
        j = i;
        while (j > 0 && v->items[pl->items[j - 1]].pid > v->items[key].pid) {
            pl->items[j] = pl->items[j - 1];
            --j;
        }
        pl->items[j] = key;
    }
    (void)pos_is_less;
}

static int pos_is_less(const void *a, const void *b)
{
    (void)a;
    (void)b;
    return 0;
}

static void tree_push(TreeLines *tl, const char *text, int pid, int depth)
{
    TreeLine line;
    if (tl->count == tl->cap) {
        tl->cap = tl->cap ? tl->cap * 2 : 128;
        tl->items = xrealloc(tl->items, tl->cap * sizeof *tl->items);
    }
    memset(&line, 0, sizeof line);
    sstrcpy(line.text, sizeof line.text, text);
    line.pid = pid;
    line.depth = depth;
    tl->items[tl->count++] = line;
}

typedef struct {
    const ProcVec *vec;
    const PosList *children; /* indexed by position in vec */
    unsigned char *visited;
    TreeLines *out;
    int max_depth;
    int utf8; /* 1 = Unicode branch art, 0 = ASCII */
} RenderCtx;

static void render_node(const RenderCtx *ctx, size_t pos,
                        const char *prefix, const char *branch, int depth)
{
    const ProcessInfo *pi = &ctx->vec->items[pos];
    char line[TREE_TEXT_MAX];
    char label[160];
    size_t k;

    if (ctx->visited && ctx->visited[pos])
        return;

    snprintf(label, sizeof label, "%s (%d) [%s]", pi->name, pi->pid,
             pi->state_name);
    if (depth == 0)
        snprintf(line, sizeof line, "%s", label);
    else
        snprintf(line, sizeof line, "%s%s%s", prefix, branch, label);
    tree_push(ctx->out, line, pi->pid, depth);
    if (ctx->visited)
        ctx->visited[pos] = 1;

    if (depth >= ctx->max_depth) {
        char deeper[TREE_TEXT_MAX];
        snprintf(deeper, sizeof deeper, "%s%s...", prefix,
                 depth > 0 ? branch : "");
        tree_push(ctx->out, deeper, 0, depth + 1);
        return;
    }

    {
        const PosList *kids = &ctx->children[pos];
        const char *tee = ctx->utf8 ? "├── " : "|-- ";
        const char *elbow = ctx->utf8 ? "└── " : "`-- ";
        const char *pipe = ctx->utf8 ? "│   " : "|   ";
        const char *blank = ctx->utf8 ? "    " : "    ";

        for (k = 0; k < kids->count; ++k) {
            size_t cpos = kids->items[k];
            int last = (k + 1 == kids->count);
            const char *cbranch = last ? elbow : tee;
            char child_prefix[TREE_TEXT_MAX];

            /* Grandchildren continuation depends on whether THIS node is
             * the last sibling (its own branch is the elbow): the vertical
             * pipe continues only under non-last nodes. */
            if (depth == 0)
                child_prefix[0] = '\0';
            else if (branch == elbow) /* this node is the last sibling */
                snprintf(child_prefix, sizeof child_prefix, "%s%s", prefix,
                         blank);
            else
                snprintf(child_prefix, sizeof child_prefix, "%s%s", prefix,
                         pipe);

            render_node(ctx, cpos, child_prefix, cbranch, depth + 1);
        }
    }
}

int tree_build(const ProcVec *vec, TreeLines *out)
{
    size_t n, i;
    PosList *children;
    int *pos_of_pid = NULL;
    int pid_max = 0;
    unsigned char *visited = NULL;
    RenderCtx ctx;

    if (!vec || !out)
        return -1;
    out->items = NULL;
    out->count = 0;
    out->cap = 0;

    n = vec->count;
    if (n == 0)
        return 0;

    for (i = 0; i < n; ++i)
        if (vec->items[i].pid > pid_max)
            pid_max = vec->items[i].pid;

    children = xcalloc(n, sizeof *children);
    pos_of_pid = xmalloc(((size_t)pid_max + 1) * sizeof *pos_of_pid);
    for (i = 0; i <= (size_t)pid_max; ++i)
        pos_of_pid[i] = -1;
    for (i = 0; i < n; ++i) {
        int pid = vec->items[i].pid;
        if (pid >= 0 && pid <= pid_max)
            pos_of_pid[pid] = (int)i;
    }

    /* bucket children by parent position (skip self-parents: malformed) */
    for (i = 0; i < n; ++i) {
        int ppid = vec->items[i].ppid;
        int pidx = (ppid >= 0 && ppid <= pid_max) ? pos_of_pid[ppid] : -1;
        if (pidx >= 0 && (size_t)pidx != i)
            poslist_push(&children[pidx], i);
    }
    for (i = 0; i < n; ++i) {
        if (children[i].count > 1)
            sort_children_by_pid(&children[i], vec);
    }

    visited = xcalloc(n, 1);
    ctx.vec = vec;
    ctx.children = children;
    ctx.visited = visited;
    ctx.out = out;
    ctx.max_depth = 64;
    ctx.utf8 = 1; /* renderer default; batch mode re-renders ASCII if needed */

    /* roots: PID 1 first when present, then orphans (parent not in the
     * snapshot), ppid==0 processes, and any cycle survivors. */
    {
        int root1 = (pid_max >= 1) ? pos_of_pid[1] : -1;
        if (root1 >= 0)
            render_node(&ctx, (size_t)root1, "", "", 0);
        for (i = 0; i < n; ++i) {
            const ProcessInfo *pi = &vec->items[i];
            int ppid = pi->ppid;
            int pidx = (ppid >= 0 && ppid <= pid_max) ? pos_of_pid[ppid] : -1;
            if (!visited[i] &&
                (ppid == 0 || pidx < 0 || ppid == pi->pid || visited[pidx]))
                render_node(&ctx, i, "", "", 0);
        }
        /* anything still unvisited sits inside a malformed cycle */
        for (i = 0; i < n; ++i)
            if (!visited[i])
                render_node(&ctx, i, "", "", 0);
    }

    for (i = 0; i < n; ++i)
        free(children[i].items);
    free(children);
    free(pos_of_pid);
    free(visited);
    return 0;
}

void tree_lines_free(TreeLines *tl)
{
    if (!tl)
        return;
    free(tl->items);
    tl->items = NULL;
    tl->count = 0;
    tl->cap = 0;
}
