/* self_audit.c — Self-audit framework for ZXV event-space
 *
 * Built-in invariant checks that run at every state transition and
 * at periodic intervals. Designed to detect:
 *   - Queue overflow / underflow inconsistencies
 *   - Budget invariant violations
 *   - Excessive fault rates (degrading domains)
 *   - Quarantine expiry (domains ready to rejoin)
 *   - Orphan RUNNING domains (sequencer not dispatching)
 *
 * Every audit check returns a result that feeds directly into the
 * self-healing engine. The audit framework is paraconsistent: it
 * preserves WARN states rather than collapsing them to PASS or FAIL.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "event_space.h"

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void ev_audit_init(ev_audit_t *audit) {
    if (!audit) return;
    ev_memset(audit, 0, sizeof(*audit));

    /* Register built-in checks */
    ev_audit_register_check(audit, "queue_overflow",
                             ev_audit_check_queue_overflow, NULL);
    ev_audit_register_check(audit, "budget_invariant",
                             ev_audit_check_budget_invariant, NULL);
    ev_audit_register_check(audit, "fault_rate",
                             ev_audit_check_fault_rate, NULL);
    ev_audit_register_check(audit, "quarantine_expiry",
                             ev_audit_check_quarantine_expiry, NULL);

    audit->invariant_queue_overflow = false;
    audit->invariant_budget_exceeded = false;
    audit->invariant_orphan_running = false;
}

bool ev_audit_register_check(ev_audit_t *audit, const char *name,
                              ev_audit_check_fn fn, void *context) {
    if (!audit || !name || !fn) return false;
    if (audit->num_checks >= EV_MAX_AUDIT_CHECKS) return false;

    ev_audit_check_t *check = &audit->checks[audit->num_checks++];
    copy_str(check->name, name, EV_DOMAIN_NAME_LEN);
    check->fn = fn;
    check->context = context;
    check->enabled = true;
    check->pass_count = 0;
    check->fail_count = 0;
    return true;
}

ev_audit_result_t ev_audit_check_domain(ev_sequencer_t *seq,
                                         ev_audit_t *audit,
                                         ev_domain_t *domain) {
    if (!seq || !audit || !domain) return EV_AUDIT_FAIL;

    ev_audit_result_t worst = EV_AUDIT_PASS;

    for (uint32_t i = 0; i < audit->num_checks; i++) {
        if (!audit->checks[i].enabled) continue;

        ev_audit_result_t result = audit->checks[i].fn(seq, domain,
                                                        audit->checks[i].context);
        seq->total_audit_checks_run++;

        if (result > worst) worst = result;

        if (result <= EV_AUDIT_WARN) {
            audit->checks[i].pass_count++;
        } else {
            audit->checks[i].fail_count++;
            seq->total_audit_failures++;
        }
    }

    return worst;
}

ev_audit_result_t ev_audit_check_system(ev_sequencer_t *seq,
                                         ev_audit_t *audit) {
    if (!seq || !audit) return EV_AUDIT_FAIL;

    ev_audit_result_t worst = EV_AUDIT_PASS;

    /* Check each domain */
    for (uint32_t i = 0; i < EV_MAX_DOMAINS; i++) {
        ev_domain_t *d = &seq->domains[i];
        if (d->state == EV_DOMAIN_UNUSED || d->state == EV_DOMAIN_TERMINATED)
            continue;

        ev_audit_result_t result = ev_audit_check_domain(seq, audit, d);
        if (result > worst) worst = result;
    }

    /* System-level invariants */
    bool any_running = false;
    for (uint32_t i = 0; i < EV_MAX_DOMAINS; i++) {
        if (seq->domains[i].state == EV_DOMAIN_RUNNING) {
            any_running = true;
            break;
        }
    }

    /* If domains are RUNNING but we're in audit (not dispatch), that's
     * an orphan RUNNING state — a potential invariant violation. */
    audit->invariant_orphan_running = any_running;
    if (any_running) {
        worst = (worst < EV_AUDIT_WARN) ? EV_AUDIT_WARN : worst;
    }

    /* Check for queue overflow across all domains */
    for (uint32_t i = 0; i < EV_MAX_DOMAINS; i++) {
        ev_domain_t *d = &seq->domains[i];
        if (d->state == EV_DOMAIN_UNUSED) continue;
        if (d->queue_count > EV_MAX_QUEUE_DEPTH) {
            audit->invariant_queue_overflow = true;
            worst = EV_AUDIT_QUARANTINE;
            break;
        }
        if (d->events_remaining > d->event_budget) {
            audit->invariant_budget_exceeded = true;
            worst = EV_AUDIT_QUARANTINE;
            break;
        }
    }

    audit->last_full_audit_sequence = seq->next_sequence;
    seq->last_audit_sequence = (uint32_t)seq->next_sequence;

    return worst;
}

/* ===== Built-in audit checks ===== */

ev_audit_result_t ev_audit_check_queue_overflow(ev_sequencer_t *seq,
                                                 ev_domain_t *domain,
                                                 void *context) {
    (void)seq;
    (void)context;
    if (!domain) return EV_AUDIT_FAIL;

    /* Queue count must never exceed max depth */
    if (domain->queue_count > EV_MAX_QUEUE_DEPTH) {
        return EV_AUDIT_QUARANTINE;
    }

    /* Queue head/tail must be within bounds */
    if (domain->queue_head >= EV_MAX_QUEUE_DEPTH ||
        domain->queue_tail >= EV_MAX_QUEUE_DEPTH) {
        return EV_AUDIT_QUARANTINE;
    }

    /* If queue is full, warn — domain may be overloaded */
    if (domain->queue_count == EV_MAX_QUEUE_DEPTH) {
        return EV_AUDIT_WARN;
    }

    return EV_AUDIT_PASS;
}

ev_audit_result_t ev_audit_check_budget_invariant(ev_sequencer_t *seq,
                                                   ev_domain_t *domain,
                                                   void *context) {
    (void)seq;
    (void)context;
    if (!domain) return EV_AUDIT_FAIL;

    /* events_remaining must never exceed event_budget */
    if (domain->events_remaining > domain->event_budget) {
        return EV_AUDIT_FAIL;
    }

    /* If domain has exceeded budget frequently, warn about degradation */
    if (domain->total_over_budget > 0 &&
        domain->total_over_budget > domain->total_dispatches / 4) {
        /* Over-budget on more than 25% of dispatches — degrading */
        return EV_AUDIT_WARN;
    }

    return EV_AUDIT_PASS;
}

ev_audit_result_t ev_audit_check_fault_rate(ev_sequencer_t *seq,
                                             ev_domain_t *domain,
                                             void *context) {
    (void)seq;
    (void)context;
    if (!domain) return EV_AUDIT_FAIL;

    /* Consecutive faults indicate a persistent problem */
    if (domain->consecutive_faults >= 10) {
        return EV_AUDIT_QUARANTINE;
    }
    if (domain->consecutive_faults >= 5) {
        return EV_AUDIT_FAIL;
    }
    if (domain->consecutive_faults >= 3) {
        return EV_AUDIT_WARN;
    }

    /* Total fault rate check */
    if (domain->total_dispatches > 100) {
        uint64_t fault_rate = domain->total_faults * 100 / domain->total_dispatches;
        if (fault_rate > 50) {
            return EV_AUDIT_FAIL;
        }
        if (fault_rate > 25) {
            return EV_AUDIT_WARN;
        }
    }

    return EV_AUDIT_PASS;
}

ev_audit_result_t ev_audit_check_quarantine_expiry(ev_sequencer_t *seq,
                                                    ev_domain_t *domain,
                                                    void *context) {
    (void)context;
    if (!domain || !seq) return EV_AUDIT_FAIL;

    /* If domain is quarantined, check if quarantine period has expired */
    if (domain->state == EV_DOMAIN_QUARANTINED) {
        if (seq->next_sequence >= domain->quarantine_until_sequence) {
            /* Quarantine expired — domain is eligible for recovery */
            return EV_AUDIT_WARN;  /* not a failure, but needs attention */
        }
    }

    return EV_AUDIT_PASS;
}
