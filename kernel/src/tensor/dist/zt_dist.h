/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_dist.h — transports for tensor-parallel inference (zt_model.h T23), for
 * HOSTED builds only (the app, bench/, tests, HPC launchers). Kernel images
 * never link zt_dist*.c.
 *
 * A group of `size` processes (ranks) runs one model together: each computes
 * its rows of every matrix product and its query heads of attention, and an
 * all-gather gives every rank the whole result (T23). The logits are bit for
 * bit those of one process, whatever the transport.
 *
 *   D1 RING. Rank r sends to rank r + 1 and receives from rank r - 1 (mod
 *      size) over two connected byte streams (socketpairs, TCP, or any fd
 *      pair). An all-gather is size - 1 steps; in step t rank r sends the
 *      part that started at rank r - t and receives the part of rank
 *      r - t - 1, so every link carries (size - 1) / size of the data once.
 *      Sending and receiving overlap (poll), so a step cannot deadlock
 *      however large the parts are.
 *   D2 WIRE. Each message is the part's int32 values, little-endian, vector
 *      by vector, then the part's saturation count (uint32 LE). Both sides
 *      know every part's size from (n, unit, size), so there is no header.
 *      A peer that closes, or sends nothing for timeout_ms, fails the call
 *      (ZT_MODEL_EDIST from the forward pass).
 *   D3 LAUNCHERS. zt_dist_fork_ring starts size - 1 child processes on this
 *      machine joined by socketpairs (tests, one-box runs).
 *      zt_dist_tcp_ring joins ranks on different machines over TCP: rank r
 *      listens on port + r and connects to rank r + 1's host at
 *      port + r + 1. zt_dist_mpi.c offers the same group over MPI
 *      (MPI_Allgatherv) when built with an MPI compiler.
 *   HONEST LIMITS. Plain TCP: no encryption or authentication. Use it only
 *   on a trusted cluster network; the PQ mesh (Vinea session records) is the
 *   planned transport between untrusted nodes (docs/HPC_DESIGN.md). One
 *   forward pass at a time per group. IPv4 host names or addresses only. */
#ifndef ZT_DIST_H
#define ZT_DIST_H

#include <stdint.h>
#include <sys/types.h>

#include "zt_model.h"

typedef struct zt_dist_ring zt_dist_ring_t;

/* A ring rank over fd_next (to rank + 1) and fd_prev (from rank - 1); it
 * owns both fds from now on (they are set non-blocking and closed by
 * zt_dist_ring_free). size 1 needs no fds (-1). Null on allocation failure. */
zt_dist_ring_t *zt_dist_ring_new(uint32_t rank, uint32_t size, int fd_next, int fd_prev,
                                 uint32_t timeout_ms);
void zt_dist_ring_free(zt_dist_ring_t *r);
/* The zt_dist_t for zt_model_t.dist (valid while r lives). */
const zt_dist_t *zt_dist_ring_dist(zt_dist_ring_t *r);
/* Bytes this rank has sent, and all-gathers it has run. */
uint64_t zt_dist_ring_bytes_sent(const zt_dist_ring_t *r);
uint64_t zt_dist_ring_calls(const zt_dist_ring_t *r);

/* Fork size - 1 children joined in a ring by socketpairs. Returns the rank of
 * the calling process (0 in the parent, 1..size-1 in each child) with its
 * fds, or -1 on failure. In the parent, pids[0..size-2] receive the
 * children's pids (pids may be null). */
int zt_dist_fork_ring(uint32_t size, int *fd_next, int *fd_prev, pid_t *pids);

/* Join a ring of `size` ranks over TCP: hosts[i] is rank i's host name or
 * IPv4 address. Listens on port + rank (all interfaces), connects to
 * hosts[rank + 1] at port + rank + 1, retrying for up to timeout_ms.
 * Returns 0 with the two fds, or -1. */
int zt_dist_tcp_ring(uint32_t rank, uint32_t size, const char *const *hosts, uint16_t port,
                     uint32_t timeout_ms, int *fd_next, int *fd_prev);

#endif /* ZT_DIST_H */
