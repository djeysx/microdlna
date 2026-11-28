#pragma once
/* Spawns the real microdlnad binary as a child process for black-box
 * functional tests, waits for it to start accepting HTTP connections, and
 * stops it again (graceful SIGTERM, SIGKILL as a last resort). */

#include <sys/types.h>

struct mdlna_proc
{
    pid_t pid;
    int port;
};

/* Starts "$MICRODLNAD_BIN -g -p <port> -D <media_dir> -i lo <extra_args...>"
 * (MICRODLNAD_BIN defaults to "build/microdlnad"), then blocks until the
 * HTTP port accepts a connection. extra_args is a NULL-terminated array of
 * extra argv entries appended last (pass NULL for none), so they override
 * the defaults (getopt keeps the last value).
 * Aborts the test if the process exits before listening, or if it never
 * starts listening within a few seconds -- use mdlna_expect_fast_exit()
 * instead for scenarios that are *supposed* to fail at startup. */
void mdlna_start(struct mdlna_proc *p, const char *media_dir, int port,
                  const char *const *extra_args);

/* For scenarios where the server is expected to exit quickly instead of
 * ever accepting connections (e.g. invalid CLI arguments). Returns the
 * exit status (as from waitpid()'s wstatus, inspect with WIFEXITED/
 * WEXITSTATUS) if the process exited within the deadline. Aborts the test
 * if the process starts accepting connections instead (wrong scenario for
 * this entry point) or if it neither exits nor starts listening in time. */
int mdlna_expect_fast_exit(const char *media_dir, int port,
                            const char *const *extra_args);

/* SIGTERM, then poll for exit for up to ~2s, then SIGKILL as a last
 * resort. Safe to call once per started process; every TEST() must call
 * this on every exit path (the harness's ASSERT_* does not unwind, so an
 * early return after a failed assertion skips whatever comes after it).
 * Returns 1 if the process exited on its own (via SIGTERM) with status
 * EXIT_SUCCESS before the grace period elapsed, 0 otherwise (timed out and
 * was SIGKILLed, or exited with a non-zero status). */
int mdlna_stop(struct mdlna_proc *p);

/* Same as mdlna_stop(), but sends `sig` instead of SIGTERM first. */
int mdlna_stop_with(struct mdlna_proc *p, int sig);
