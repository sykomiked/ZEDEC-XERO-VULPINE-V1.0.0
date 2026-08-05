/* naga_raja.c — Naga Raja: the Interface to Tantra
 *
 * A thin, paraconsistent-result wrapper around tantra_engine_t (which
 * owns the Dharma set) and the RMAG/LPRES ledgers IPC is built on.
 * Naga Raja does not schedule or weave anything itself.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "naga_raja.h"
#include "../rmag/rmag_core.h"
#include "../lpres/lpres_core.h"
#include <stddef.h>

static tantra_engine_t *s_engine = NULL;

void naga_raja_init(tantra_engine_t *engine) {
    s_engine = engine;
}

naga_result_t naga_raja_spawn(uint32_t task_id) {
    naga_result_t r = {TRIT_FALSE, 0};
    if (!s_engine) return r;
    ordinal_t now = s_engine->oseq ? s_engine->oseq->current_cycle : 0;
    dharma_on_spawn(&s_engine->dharma, task_id, now);
    r.status = TRIT_TRUE;
    r.value = (int32_t)task_id;
    return r;
}

naga_result_t naga_raja_yield(uint32_t task_id) {
    naga_result_t r = {TRIT_FALSE, 0};
    (void)task_id;
    if (!s_engine) return r;
    tantra_result_t tr = tantra_run(s_engine);
    r.status = (tr == TANTRA_CLEAR) ? TRIT_TRUE : TRIT_GLUT_NEUTRAL;
    return r;
}

naga_result_t naga_raja_sleep(uint32_t task_id) {
    naga_result_t r = {TRIT_FALSE, 0};
    if (!s_engine) return r;
    ordinal_t now = s_engine->oseq ? s_engine->oseq->current_cycle : 0;
    dharma_on_sleep(&s_engine->dharma, task_id, now);
    r.status = TRIT_TRUE;
    return r;
}

naga_result_t naga_raja_block(uint32_t task_id) {
    naga_result_t r = {TRIT_FALSE, 0};
    if (!s_engine) return r;
    ordinal_t now = s_engine->oseq ? s_engine->oseq->current_cycle : 0;
    dharma_on_block(&s_engine->dharma, task_id, now);
    r.status = TRIT_TRUE;
    return r;
}

naga_result_t naga_raja_terminate(uint32_t task_id, int32_t exit_code) {
    naga_result_t r = {TRIT_FALSE, 0};
    if (!s_engine) return r;
    ordinal_t now = s_engine->oseq ? s_engine->oseq->current_cycle : 0;
    dharma_on_terminate(&s_engine->dharma, task_id, now);
    r.status = TRIT_TRUE;
    r.value = exit_code;
    return r;
}

naga_result_t naga_raja_send(uint32_t sender_id, uint32_t receiver_id,
                              nr_resource_kind_t kind, rational_t amount) {
    (void)kind;
    naga_result_t r = {TRIT_FALSE, 0};
    if (!s_engine) return r;

    rational_t sender_quota = rmag_get_quota((ordinal_t)sender_id);
    rational_t receiver_quota = rmag_get_quota((ordinal_t)receiver_id);

    /* Determine the actual transferable amount: min(sender_quota, amount) */
    rational_t transferable = amount;
    /* Compare sender_quota vs amount using rational_mag (double approximation
     * for comparison only -- the actual transfer uses exact rationals) */
    double sq = rational_mag(sender_quota);
    double am = rational_mag(amount);
    if (sq < am) {
        transferable = sender_quota; /* partial: send what's available */
    }

    /* Subtract from sender, add to receiver */
    rational_t new_sender = rmag_sub_quotas(sender_quota, transferable);
    rational_t new_receiver = rmag_add_quotas(receiver_quota, transferable);
    rmag_set_quota((ordinal_t)sender_id, new_sender);
    rmag_set_quota((ordinal_t)receiver_id, new_receiver);

    /* Set receiver's LPRES presence so they know a message is waiting */
    lpres_set_presence((ordinal_t)receiver_id, TRIT_TRUE);

    /* Determine status: full transfer vs partial */
    if (sq >= am) {
        r.status = TRIT_TRUE;
    } else {
        r.status = TRIT_GLUT_MINUS; /* partial delivery contradiction */
    }
    r.value = (int32_t)(rational_mag(transferable) * 1000); /* milli-quota transferred */
    return r;
}

naga_result_t naga_raja_recv(uint32_t task_id) {
    naga_result_t r = {TRIT_FALSE, 0};
    if (!s_engine) return r;

    trit_t presence = lpres_get_presence((ordinal_t)task_id);
    if (presence != TRIT_TRUE) {
        r.status = TRIT_FALSE;
        return r;
    }

    rational_t quota = rmag_get_quota((ordinal_t)task_id);
    if (quota.num == 0) {
        r.status = TRIT_FALSE;
        return r;
    }

    /* Claim the quota: clear it back to zero */
    rmag_set_quota((ordinal_t)task_id, (rational_t){0, 1});
    lpres_set_presence((ordinal_t)task_id, TRIT_FALSE);

    r.status = TRIT_TRUE;
    r.value = (int32_t)(rational_mag(quota) * 1000); /* milli-quota received */
    return r;
}

trit_t naga_raja_presence(uint32_t task_id) {
    return lpres_get_presence((ordinal_t)task_id);
}

l13_phase_t naga_raja_phase(uint32_t task_id) {
    if (!s_engine) return (l13_phase_t)0;
    return dharma_task_phase(&s_engine->dharma, task_id);
}
