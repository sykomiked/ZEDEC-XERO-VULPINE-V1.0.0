/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_rpc_client.h — the native shell's side of zxvd's control socket
 * (kernel/arch/hosted/zxvd_rpc.h, docs/ZXVD.md): one JSON-RPC request line
 * out, one reply line back, over the owner-only UNIX socket. Plain POSIX C,
 * so it is tested on Linux against the real daemon (test_rpc_client.py) and
 * compiled unchanged into ZXVApp.m. Blocking: call it off the main thread. */
#ifndef ZXV_RPC_CLIENT_H
#define ZXV_RPC_CLIENT_H

#include <stddef.h>

/* Send `request` (one JSON object, no newline) and read the reply line into
 * reply[cap] (NUL-terminated, newline removed). Every send and receive waits
 * at most timeout_ms. Returns the reply length, or -1: no socket, refused,
 * timed out, closed early, or a reply that does not fit. */
long zxv_rpc_call(const char *sock_path, const char *request, char *reply, size_t cap,
                  int timeout_ms);

/* The "unread" count in a notes.list reply, or -1 when the reply is an
 * error or has none. */
long zxv_rpc_unread(const char *reply);

#endif
