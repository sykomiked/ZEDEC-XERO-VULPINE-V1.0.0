/* naga_raja.h — Naga Raja: the Interface to Tantra
 *
 * The syscall-style boundary user-space/apps call through. Every
 * call returns a naga_result_t whose status is a trit_t, never a
 * binary success/fail int: a GLUT status means the operation is
 * genuinely, admissibly contradictory (e.g. a message partially
 * delivered to a receiver in an ambiguous presence state), reported
 * as such rather than forced into a hard error or silent success.
 *
 * Naga Raja does not schedule or weave anything itself -- it is a
 * thin, paraconsistent-result wrapper around tantra_engine_t (which
 * owns the Dharma set) and the RMAG/LPRES ledgers IPC is built on.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef NAGA_RAJA_H
#define NAGA_RAJA_H

#include "m5_types.h"
#include "tantra.h"

typedef struct naga_result {
    trit_t status;
    int32_t value;   /* ancillary: task id, bytes/quota transferred, etc. */
} naga_result_t;

/* Resource kinds Naga Raja is aware of, including the holographic
 * container file types (.36n9 positive space, .9n63 negative space,
 * .36m9 dataset, .zedei renderer, .zedec container) so IPC payloads
 * and resource handles can be typed against them even before every
 * kind has a full I/O implementation wired through holo.c. */
typedef enum {
    NR_RES_RAW = 0,
    NR_RES_36N9,
    NR_RES_9N63,
    NR_RES_36M9,
    NR_RES_ZEDEI,
    NR_RES_ZEDEC,
} nr_resource_kind_t;

void naga_raja_init(tantra_engine_t *engine);

naga_result_t naga_raja_spawn(uint32_t task_id);      /* emits the Ain-Soph-Aur spawn karma for task_id */
naga_result_t naga_raja_yield(uint32_t task_id);       /* runs one tantra_run() cycle */
naga_result_t naga_raja_sleep(uint32_t task_id);       /* -> Netzach (7) */
naga_result_t naga_raja_block(uint32_t task_id);       /* -> Hod (8) */
naga_result_t naga_raja_terminate(uint32_t task_id, int32_t exit_code);

/* IPC as a ledger operation: transfers an RMAG rational quota (the
 * message's resource cost) from sender to receiver, attested via
 * LPRES presence, rather than a conventional byte-stream copy. If the
 * sender's quota is insufficient, transfers whatever IS available and
 * returns TRIT_GLUT_MINUS (a genuine partial-delivery contradiction),
 * never a hard failure. */
naga_result_t naga_raja_send(uint32_t sender_id, uint32_t receiver_id, nr_resource_kind_t kind,
                              rational_t amount);
/* Claims whatever quota is currently attested as present for
 * task_id, clearing its presence back to TRIT_FALSE on success. */
naga_result_t naga_raja_recv(uint32_t task_id);

trit_t naga_raja_presence(uint32_t task_id); /* direct LPRES query */
l13_phase_t naga_raja_phase(uint32_t task_id); /* direct Dharma-set query */

#endif
