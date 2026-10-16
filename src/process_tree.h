/*
 * process_tree.h — builds the process hierarchy from real PID/PPID data.
 *
 * The builder is pure (no terminal access): it consumes a ProcVec snapshot
 * and produces tagged text lines, so the TUI can scroll them and the test
 * suite can assert on the structure.
 */
#ifndef LPMCS_PROCESS_TREE_H
#define LPMCS_PROCESS_TREE_H

#include "process_monitor.h"

#define TREE_TEXT_MAX 256

typedef struct {
    char text[TREE_TEXT_MAX]; /* rendered line (branch art + info) */
    int pid;                  /* 0 for decorative lines            */
    int depth;
} TreeLine;

typedef struct {
    TreeLine *items;
    size_t count;
    size_t cap;
} TreeLines;

/*
 * Build the tree:
 *   - roots are PID 1 (when present) plus processes with PPID 0, a
 *     self-parent (defensive), or a parent missing from the snapshot
 *     (orphans — parents that exited between scans);
 *   - children are sorted by PID (deterministic output);
 *   - a visited mask guards against malformed/cyclic data.
 * Returns 0 on success.
 */
int tree_build(const ProcVec *vec, TreeLines *out);

void tree_lines_free(TreeLines *tl);

#endif /* LPMCS_PROCESS_TREE_H */
