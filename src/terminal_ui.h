/*
 * terminal_ui.h — raw-mode ANSI terminal engine and interactive views.
 *
 * Responsibilities:
 *   - termios raw mode with guaranteed restore (also on SIGINT/SIGTERM)
 *   - alternate screen, cursor hiding, UTF-8 detection
 *   - single-buffered frame building + one write() per frame (no flicker)
 *   - key parsing (arrows, PgUp/PgDn, Home/End, Enter, Esc, chars)
 *   - the interactive views: live table, details, tree, popups, demo
 */
#ifndef LPMCS_TERMINAL_UI_H
#define LPMCS_TERMINAL_UI_H

#include "process_monitor.h"
#include "system_info.h"

/* Interactive monitor loop. Returns when the user quits or a termination
 * signal arrives. Restores the terminal before returning. */
void ui_run(MonitorState *mon, ProcVec *vec, SystemStats *stats);

/* Non-interactive evidence modes (batch output, no terminal changes). */
void ui_print_batch_table(const ProcVec *vec, const SystemStats *stats);
void ui_print_batch_tree(ProcVec *vec, int utf8);

/* Ask the main loop to exit cleanly (called from signal handlers). */
void ui_request_stop(void);

/* ASCII-art renderer flag for the tree in batch mode (0 = ASCII). */
void ui_set_utf8(int enabled);

#endif /* LPMCS_TERMINAL_UI_H */
