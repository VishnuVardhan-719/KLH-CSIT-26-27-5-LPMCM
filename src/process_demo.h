/*
 * process_demo.h — fork()/exec()/waitpid() educational demonstration.
 *
 * This screen is deliberately separate from the monitoring system: the
 * monitor never relies on the demo and the demo never touches /proc.
 */
#ifndef LPMCS_PROCESS_DEMO_H
#define LPMCS_PROCESS_DEMO_H

/*
 * Run the process-creation demonstration on plain stdout. The caller is
 * responsible for leaving the alternate screen first (the UI does this).
 * Returns 0 on success, -1 if fork/exec failed.
 */
int run_process_demo(void);

#endif /* LPMCS_PROCESS_DEMO_H */
