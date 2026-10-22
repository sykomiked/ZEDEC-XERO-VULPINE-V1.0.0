/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_proc.h — the machine's processes, behind Warren: proc.list,
 * proc.signal, proc.restart.
 *
 *   P1 LIST. proc.list {"all"?: false} gives the processes of the account
 *      zxvd runs as ("all": true for every account's), at most
 *      ZXVD_PROC_MAX, with "truncated" when there were more. Each: "pid",
 *      "ppid", "name", "user" (the account that owns it; null when the
 *      name cannot be read) and "uid", "state" (running, sleeping,
 *      stopped, zombie, idle or other), "cpu_ms" (user and system time),
 *      "rss" (resident bytes), "started_ms" (ms since 1970), "sid" (its
 *      session), "session_owner" (the account that owns the session's
 *      leader: who owns the session it runs in; null when the leader is
 *      gone or hidden), "mine" (owned by zxvd's account) and "job" (the
 *      shell.* job that started it, else null). Processes the OS will not
 *      describe to zxvd are left out.
 *   P2 SIGNAL. proc.signal {"pid", "signal"?: "term" (default) | "kill" |
 *      "int" | "hup" | "stop" | "cont"} sends one signal to one process.
 *   P3 RESTART. proc.restart {"pid"} reads the process's program and
 *      arguments and its folder, sends it SIGTERM (SIGKILL two seconds
 *      later if it is still there), and once it is gone starts the same
 *      program with the same arguments in the same folder, on its own
 *      (its own session, input and output to /dev/null, never a zxvd job:
 *      it outlives zxvd). The environment is zxvd's: another process's
 *      environment is not read. It answers at once with a "restart" id;
 *      proc.list's "restarts" gives each one's "state": "stopping",
 *      "started" (with "new_pid") or "failed" (with "error"). The last
 *      ZXVD_PROC_RESTARTS are kept.
 *   P4 REFUSED. proc.signal and proc.restart refuse (ZXVD_E_PROC): pid 0
 *      and 1, zxvd itself and the process that started it, a process
 *      another account owns, one that is gone, and while process control is
 *      off. The OS's own permission check still applies.
 *   P5 TRUST. The control socket only. proc.list always answers; proc.signal
 *      and proc.restart are off until the host turns them on
 *      (zxvd_proc_enable), so a test or a fuzzer gets ZXVD_E_PROC and
 *      signals nothing. Linux and macOS; not on Windows yet (ZXVD_E_PROC).
 */
#ifndef ZXVD_PROC_H
#define ZXVD_PROC_H

#include <stdbool.h>
#include <stdint.h>

#include "zxvd_rpc.h"

#define ZXVD_E_PROC        (-32028)
#define ZXVD_PROC_MAX      2048u
#define ZXVD_PROC_RESTARTS 8u
#define ZXVD_PROC_ARGV_MAX (64u * 1024u) /* bytes of a restarted program's arguments */

/* P5 */
void zxvd_proc_enable(bool on);
/* P3: move restarts on (the old process gone, or the SIGKILL due). */
void zxvd_proc_service(void);
/* forget the restarts (processes already started go on) */
void zxvd_proc_close(void);

/* the methods (zxvd_rpc.c's table) */
void zxvd_proc_list(zxvd_call_t *c);
void zxvd_proc_signal(zxvd_call_t *c);
void zxvd_proc_restart(zxvd_call_t *c);

#endif
