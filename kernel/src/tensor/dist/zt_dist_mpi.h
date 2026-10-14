/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_dist_mpi.h — the tensor-parallel group (zt_model.h T23) over MPI, for
 * clusters with an MPI library (Open MPI, MPICH, vendor MPIs). Build with the
 * MPI compiler wrapper (mpicc); nothing else in the tree needs MPI.
 *
 *   M1 One rank per MPI process of the communicator (MPI_COMM_WORLD or one
 *      the caller passes). The all-gather packs each rank's part (the same
 *      little-endian int32 layout as zt_dist.h D2), runs MPI_Allgatherv, and
 *      unpacks; the saturation count is an MPI_Allreduce sum of uint32.
 *      Exact integers only, so MPI's reduction order cannot change a bit.
 *   HONEST LIMITS. Tested with Open MPI on one machine (several processes),
 *   not yet on a multi-node cluster or an RDMA fabric. Buffers are packed on
 *   the host (no GPU-direct, no zero-copy). */
#ifndef ZT_DIST_MPI_H
#define ZT_DIST_MPI_H

#include <mpi.h>

#include "zt_model.h"

typedef struct zt_dist_mpi zt_dist_mpi_t;

/* A rank of comm (MPI must be initialised). Null on failure. */
zt_dist_mpi_t *zt_dist_mpi_new(MPI_Comm comm);
void zt_dist_mpi_free(zt_dist_mpi_t *d);
const zt_dist_t *zt_dist_mpi_dist(zt_dist_mpi_t *d);

#endif /* ZT_DIST_MPI_H */
