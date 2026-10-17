/* chg_mesh.c — the Chiglet mesh coordinator. See chg_mesh.h.
 *
 * The order of operations is the repository's: check invariants and bounds,
 * reserve, compute, mutate, commit. The want list is bounded and its full
 * check is explicit backpressure. The step budget bounds the traffic, so the
 * constant coordination can never become a storm.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "chg_mesh.h"

#include <stddef.h>

static bool cid_eq(const uint8_t a[IPFS_CID_LEN], const uint8_t b[IPFS_CID_LEN])
{
    for (uint32_t i = 0; i < IPFS_CID_LEN; i++)
        if (a[i] != b[i]) return false;
    return true;
}

void chg_mesh_init(chg_mesh_t *m, const chg_mesh_transport_t *t)
{
    if (!m) return;
    for (uint32_t i = 0; i < (uint32_t) sizeof *m; i++)
        ((uint8_t *) m)[i] = 0;
    if (t) m->t = *t;
}

bool chg_mesh_want(chg_mesh_t *m, const uint8_t cid[IPFS_CID_LEN], uint32_t bytes)
{
    if (!m || !cid) return false;
    for (uint32_t i = 0; i < m->nwant; i++) {
        if (cid_eq(m->want[i].cid, cid)) {  /* already wanted: count the ask */
            m->want[i].want_count++;
            return true;
        }
    }
    if (m->nwant >= CHG_MESH_WANT_MAX)   /* FULL: explicit backpressure */
        return false;
    chg_mesh_want_t *w = &m->want[m->nwant++];
    for (uint32_t i = 0; i < IPFS_CID_LEN; i++) w->cid[i] = cid[i];
    w->bytes = bytes;
    w->want_count = 1;
    return true;
}

/* Rank the wants so the most-asked-for content is served first: a simple
 * exact-integer insertion sort on want_count (descending), which keeps the
 * order deterministic and needs no allocation. */
static void rank_wants(chg_mesh_t *m)
{
    for (uint32_t i = 1; i < m->nwant; i++) {
        chg_mesh_want_t k = m->want[i];
        uint32_t j = i;
        while (j > 0 && m->want[j - 1].want_count < k.want_count) {
            m->want[j] = m->want[j - 1];
            j--;
        }
        m->want[j] = k;
    }
}

uint32_t chg_mesh_cycle(chg_mesh_t *m, chiglet_t *c, const ipfs_pin_progress_t *p)
{
    if (!m || !m->t.session_open) return 0;

    uint32_t ops = 0, live = 0;

    /* 1. refresh a session to each peer (the Vinea handshake carries the
     *    SCP-144 lattice binding inside the session layer). */
    for (uint32_t i = 0; i < m->peers; i++) {
        if (m->t.session_open(m->t.ctx, i, m->peers))
            live++;
        else
            m->refused++;
    }
    m->peers = live ? live : m->peers;

    /* 2. publish this seed's own state to each live peer: what it has pinned
     *    and confirmed, and what it is looking for. This is the coordination
     *    -- a seed that has the blocks tells the ones that do not. */
    if (m->t.state_publish) {
        for (uint32_t i = 0; i < m->peers && ops < CHG_MESH_STEPS_PER_CYCLE; i++) {
            if (m->t.state_publish(m->t.ctx, i, p, m->want, m->nwant))
                ops++;
            else
                m->refused++;
        }
    }

    /* 3. the Chiglet's view: with a model loaded it infers over this seed's
     *    own evidence -- what it has confirmed, what it could not confirm,
     *    what it wants and what it has served -- and the inference's verdict
     *    decides whether the coordinator keeps serving or pulls first.
     *    Without a model the deterministic ask-order stands: the coordinator
     *    still coordinates, it simply does not infer. */
    rank_wants(m);
    if (c) {
        surplus_real_t ev[1][CHG_DIM];
        chg_result_t verdict;
        for (uint32_t i = 0; i < CHG_DIM; i++) ev[0][i] = 0;
        ev[0][0] = (surplus_real_t) (p ? p->confirmed : 0u);
        ev[0][1] = (surplus_real_t) (p ? p->unconfirmed : 0u);
        ev[0][2] = (surplus_real_t) m->nwant;
        ev[0][3] = (surplus_real_t) m->served;
        for (uint32_t i = 0; i < CHG_DIM; i++) m->evidence[i] = ev[0][i];
        if (chg_infer(c, ev, 1, &verdict) == CHG_OK) {
            /* the verdict's top score: when the Chiglet cannot separate the
             * evidence (LOW_MARGIN) this seed is short of what it wants, so
             * it spends the cycle asking rather than announcing */
            m->pull = (verdict.reason == CHG_REASON_LOW_MARGIN) ? true : m->pull;
        }
    }

    /* 4. the serve requests, in ranked order, inside the step budget. */
    if (m->t.serve_request) {
        for (uint32_t w = 0; w < m->nwant && ops < CHG_MESH_STEPS_PER_CYCLE; w++) {
            for (uint32_t i = 0; i < m->peers && ops < CHG_MESH_STEPS_PER_CYCLE; i++) {
                if (m->t.serve_request(m->t.ctx, i, m->want[w].cid)) {
                    m->served++;
                    ops++;
                } else {
                    m->refused++;
                }
            }
        }
    }

    m->cycles++;
    return ops;
}
