/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_shell.h — programs run for the terminal (Flint) and for scripts:
 * shell.run, shell.read, shell.write, shell.kill, shell.jobs.
 *
 *   H1 RUN. shell.run starts one job and answers at once with its id. A job
 *      is a zxsh line ("line": run as `zxsh -c LINE`, so pipes, redirects,
 *      && and ||, and zxsh's four verdicts work as at its prompt;
 *      tools/zxsh) or one program with its arguments ("argv": no shell, the
 *      program found on PATH). The job runs beside zxvd's loop, never in
 *      it: zxvd reads its output as it comes (zxvd_shell_fds/_service), and
 *      shell.read also reads whatever is waiting.
 *   H2 OUTPUT. stdout and stderr share one pipe, so they keep the order the
 *      job wrote them in. The last ZXVD_SHELL_KEEP bytes are kept; shell.read
 *      {"from": offset} returns what came after the offset ("next" is the
 *      offset to pass back) and "dropped": the bytes that went before the
 *      reader asked. Text is UTF-8: a byte that is not becomes U+FFFD, and a
 *      character cut by the end of a read is left for the next one.
 *   H3 INPUT. shell.write sends text to the job's stdin; "close": true ends
 *      it. There is no terminal (pty): a program that needs one sees pipes.
 *   H4 END. Once the job's process has ended, shell.read says done, with
 *      the exit code or the signal and the verdict. A zxsh line's verdict
 *      is zxsh's exit status (0 true, 1 false, 4 both, 5 neither, 3
 *      forbidden: zxsh quarantined it; any other code false). A program's
 *      follows zxsh's rule for a process: 0 true, 1-125 false, 126 or 127
 *      neither (it could not run), a crash (SIGSEGV, SIGBUS, SIGILL,
 *      SIGFPE, SIGABRT, SIGSYS, SIGTRAP) forbidden, any other signal
 *      neither.
 *   H5 LIMITS. At most ZXVD_SHELL_JOBS jobs. A finished job is kept until
 *      it has been read to the end, or killed; a new job takes the oldest
 *      finished one's place when every place is taken. A job runs at most
 *      timeout_ms (default ZXVD_SHELL_TIMEOUT_MS) and is then ended like
 *      shell.kill: SIGTERM to its process group, SIGKILL two seconds later.
 *      zxvd_shell_close (zxvd stopping) ends every job the same way.
 *   H6 WHERE. "cwd": the folder it starts in (default zxvd's own). The
 *      environment is zxvd's, plus ZXSH_JOB=<id>.
 *   H7 TRUST. The control socket only, never a phone. A job runs as the
 *      owner with the owner's OS permissions, as in the terminal it serves.
 *      Nothing runs until the host turns this on with the path to zxsh
 *      (zxvd_shell_enable), so a test or a fuzzer that sends shell.run gets
 *      ZXVD_E_SHELL and starts nothing. Not on Windows yet (ZXVD_E_SHELL).
 */
#ifndef ZXVD_SHELL_H
#define ZXVD_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#include "zxvd_rpc.h"

#define ZXVD_E_SHELL           (-32023)
#define ZXVD_SHELL_JOBS        16u
#define ZXVD_SHELL_KEEP        (256u * 1024u)
#define ZXVD_SHELL_READ_MAX    (32u * 1024u) /* bytes one shell.read returns */
#define ZXVD_SHELL_LINE_MAX    8000u
#define ZXVD_SHELL_ARGS_MAX    64u
#define ZXVD_SHELL_TIMEOUT_MS  (10u * 60u * 1000u)
#define ZXVD_SHELL_TIMEOUT_TOP (24u * 60u * 60u * 1000u)

/* H7: zxsh's path ("" or NULL turns shell.* off; jobs already running go
 * on). 0, or -1 when the path is too long. */
int zxvd_shell_enable(const char *zxsh_path);
/* H1: add each job's output pipe to `set` (an fd_set *); the highest
 * descriptor, at least `max`. */
int zxvd_shell_fds(void *set, int max);
/* H1, H5: read what `set` says is ready, reap ended jobs, end late ones. */
void zxvd_shell_service(void *set);
/* H5: end every job and forget them. */
void zxvd_shell_close(void);

/* The job whose process group is `pgid` (its first process and what that
 * started), while zxvd keeps the job; else 0. For proc.list. */
uint32_t zxvd_shell_job_of(long pgid);

/* the methods (zxvd_rpc.c's table) */
void zxvd_shell_run(zxvd_call_t *c);
void zxvd_shell_read(zxvd_call_t *c);
void zxvd_shell_write(zxvd_call_t *c);
void zxvd_shell_kill(zxvd_call_t *c);
void zxvd_shell_jobs(zxvd_call_t *c);

#endif
