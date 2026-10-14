/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_dist_mpi.c — zt_dist_mpi.h. Build with mpicc. */
#include "zt_dist_mpi.h"

#include <stdlib.h>

struct zt_dist_mpi {
    zt_dist_t d;
    MPI_Comm comm;
    int *cnts, *displs;
    uint8_t *sbuf, *rbuf;
    uint64_t scap, rcap;
};

static int grow(uint8_t **b, uint64_t *cap, uint64_t need)
{
    if (need <= *cap) return 0;
    uint8_t *n = realloc(*b, (size_t) need);
    if (!n) return -1;
    *b = n;
    *cap = need;
    return 0;
}

static int mpi_allgather(void *ctx, zt_fx *y, uint32_t ystride, uint32_t B, uint32_t n,
                         uint32_t unit, uint32_t *count)
{
    zt_dist_mpi_t *d = ctx;
    uint32_t P = d->d.size, me = d->d.rank, nu = unit ? n / unit : 0, u0, u1;
    if (P < 2) return 0;
    uint64_t total = 0;
    for (uint32_t q = 0; q < P; q++) {
        zt_dist_part(nu, P, q, &u0, &u1);
        uint64_t bytes = (uint64_t) B * (u1 - u0) * unit * 4;
        if (bytes > 0x7fffffffu || total > 0x7fffffffu) return -1; /* int counts */
        d->cnts[q] = (int) bytes;
        d->displs[q] = (int) total;
        total += bytes;
    }
    if (grow(&d->sbuf, &d->scap, (uint64_t) d->cnts[me] + 4) || grow(&d->rbuf, &d->rcap, total + 4))
        return -1;
    zt_dist_part(nu, P, me, &u0, &u1);
    uint8_t *p = d->sbuf;
    for (uint32_t b = 0; b < B; b++)
        for (uint32_t i = u0 * unit; i < u1 * unit; i++, p += 4) {
            uint32_t v = (uint32_t) y[(uint64_t) b * ystride + i];
            p[0] = (uint8_t) v, p[1] = (uint8_t) (v >> 8), p[2] = (uint8_t) (v >> 16),
            p[3] = (uint8_t) (v >> 24);
        }
    if (MPI_Allgatherv(d->sbuf, d->cnts[me], MPI_BYTE, d->rbuf, d->cnts, d->displs, MPI_BYTE,
                       d->comm) != MPI_SUCCESS)
        return -1;
    for (uint32_t q = 0; q < P; q++) {
        if (q == me) continue;
        zt_dist_part(nu, P, q, &u0, &u1);
        const uint8_t *s = d->rbuf + d->displs[q];
        for (uint32_t b = 0; b < B; b++)
            for (uint32_t i = u0 * unit; i < u1 * unit; i++, s += 4)
                y[(uint64_t) b * ystride + i] =
                    (zt_fx) ((uint32_t) s[0] | ((uint32_t) s[1] << 8) | ((uint32_t) s[2] << 16) |
                             ((uint32_t) s[3] << 24));
    }
    unsigned int c = *count, sum = 0;
    if (MPI_Allreduce(&c, &sum, 1, MPI_UNSIGNED, MPI_SUM, d->comm) != MPI_SUCCESS) return -1;
    *count = sum;
    return 0;
}

zt_dist_mpi_t *zt_dist_mpi_new(MPI_Comm comm)
{
    int rank, size;
    if (MPI_Comm_rank(comm, &rank) != MPI_SUCCESS || MPI_Comm_size(comm, &size) != MPI_SUCCESS ||
        size < 1)
        return NULL;
    zt_dist_mpi_t *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->cnts = calloc((size_t) size, sizeof(int));
    d->displs = calloc((size_t) size, sizeof(int));
    if (!d->cnts || !d->displs) {
        zt_dist_mpi_free(d);
        return NULL;
    }
    d->comm = comm;
    d->d.rank = (uint32_t) rank;
    d->d.size = (uint32_t) size;
    d->d.allgather = mpi_allgather;
    d->d.ctx = d;
    return d;
}

void zt_dist_mpi_free(zt_dist_mpi_t *d)
{
    if (!d) return;
    free(d->cnts);
    free(d->displs);
    free(d->sbuf);
    free(d->rbuf);
    free(d);
}

const zt_dist_t *zt_dist_mpi_dist(zt_dist_mpi_t *d)
{
    return &d->d;
}
