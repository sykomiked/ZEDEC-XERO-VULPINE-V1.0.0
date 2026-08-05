/* self_healing.c — Self-healing framework for ZXV event-space
 *
 * Maps audit results to concrete healing actions: warn, suspend,
 * quarantine, restart, or terminate. The healing engine is designed
 * to be deterministic — the same audit result + fault history always
 * produces the same action.
 *
 * Recovery is automatic: quarantined domains are checked for recovery
 * eligibility at each audit cycle. If the quarantine period has expired
 * and the domain's faults have stabilized, it is returned to READY.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */
#include "event_space.h"

void ev_healing_init(ev_healing_t *healing) {
    if (!healing) return;
    ev_memset(healing, 0, sizeof(*healing));

    /* Default rules:
     *   WARN with 3+ consecutive faults  -> SUSPEND for 10 sequences
     *   FAIL with 5+ consecutive faults   -> QUARANTINE for 100 sequences
     *   QUARANTINE result with 10+ faults -> TERMINATE
     */
    ev_healing_register_rule(healing, EV_AUDIT_WARN, 3,
                              EV_HEAL_SUSPEND, 10);
    ev_healing_register_rule(healing, EV_AUDIT_FAIL, 5,
                              EV_HEAL_QUARANTINE, 100);
    ev_healing_register_rule(healing, EV_AUDIT_QUARANTINE, 10,
                              EV_HEAL_TERMINATE, 0);
}

bool ev_healing_register_rule(ev_healing_t *healing,
                               ev_audit_result_t trigger,
                               uint32_t fault_threshold,
                               ev_heal_action_t action,
                               uint32_t suspend_duration) {
    if (!healing) return false;
    if (healing->num_rules >= EV_MAX_HEALING_RULES) return false;

    ev_healing_rule_t *rule = &healing->rules[healing->num_rules++];
    rule->trigger_result = trigger;
    rule->trigger_fault_threshold = fault_threshold;
    rule->action = action;
    rule->suspend_duration = suspend_duration;
    return true;
}

ev_heal_action_t ev_healing_apply(ev_sequencer_t *seq,
                                   ev_healing_t *healing,
                                   ev_domain_t *domain,
                                   ev_audit_result_t audit_result) {
    if (!seq || !healing || !domain) return EV_HEAL_NONE;

    /* If audit passed, no healing needed */
    if (audit_result == EV_AUDIT_PASS) {
        /* If domain was suspended and audit now passes, recover */
        if (domain->state == EV_DOMAIN_BLOCKED &&
            domain->consecutive_faults == 0) {
            domain->state = EV_DOMAIN_READY;
            healing->total_auto_recoveries++;
            return EV_HEAL_NONE;
        }
        return EV_HEAL_NONE;
    }

    /* Find the first matching rule (rules are checked in registration order) */
    ev_heal_action_t action = EV_HEAL_NONE;
    uint32_t suspend_duration = 0;

    for (uint32_t i = 0; i < healing->num_rules; i++) {
        ev_healing_rule_t *rule = &healing->rules[i];
        if (rule->trigger_result == audit_result &&
            domain->consecutive_faults >= rule->trigger_fault_threshold) {
            action = rule->action;
            suspend_duration = rule->suspend_duration;
            break;
        }
    }

    /* If no rule matched but audit result is WARN, just log */
    if (action == EV_HEAL_NONE && audit_result == EV_AUDIT_WARN) {
        action = EV_HEAL_WARN;
    }

    /* If no rule matched but audit result is FAIL, suspend as default */
    if (action == EV_HEAL_NONE && audit_result == EV_AUDIT_FAIL) {
        action = EV_HEAL_SUSPEND;
        suspend_duration = 50;
    }

    /* If no rule matched but audit result is QUARANTINE, quarantine */
    if (action == EV_HEAL_NONE && audit_result == EV_AUDIT_QUARANTINE) {
        action = EV_HEAL_QUARANTINE;
        suspend_duration = 200;
    }

    /* Apply the action */
    switch (action) {
        case EV_HEAL_NONE:
            break;

        case EV_HEAL_WARN:
            /* No state change — just track */
            break;

        case EV_HEAL_SUSPEND:
            if (domain->state != EV_DOMAIN_QUARANTINED &&
                domain->state != EV_DOMAIN_TERMINATED) {
                domain->state = EV_DOMAIN_BLOCKED;
                domain->quarantine_until_sequence =
                    (uint32_t)seq->next_sequence + suspend_duration;
                healing->total_suspensions++;
            }
            break;

        case EV_HEAL_QUARANTINE:
            domain->state = EV_DOMAIN_QUARANTINED;
            domain->total_quarantines++;
            domain->quarantine_until_sequence =
                (uint32_t)seq->next_sequence + suspend_duration;
            seq->total_domains_quarantined++;
            healing->total_quarantines++;
            break;

        case EV_HEAL_RESTART:
            /* Re-initialize domain state: clear queue, reset budget */
            domain->queue_head = 0;
            domain->queue_tail = 0;
            domain->queue_count = 0;
            domain->events_remaining = domain->event_budget;
            domain->consecutive_faults = 0;
            domain->state = EV_DOMAIN_READY;
            healing->total_restarts++;
            seq->total_domains_recovered++;
            break;

        case EV_HEAL_TERMINATE:
            domain->state = EV_DOMAIN_TERMINATED;
            domain->consecutive_faults = 0;
            healing->total_terminations++;
            if (seq->num_domains > 0) seq->num_domains--;
            break;
    }

    return action;
}

bool ev_healing_check_recovery(ev_sequencer_t *seq,
                                ev_healing_t *healing,
                                ev_domain_t *domain) {
    if (!seq || !healing || !domain) return false;

    /* Only check quarantined or suspended domains */
    if (domain->state != EV_DOMAIN_QUARANTINED &&
        domain->state != EV_DOMAIN_BLOCKED) {
        return false;
    }

    /* Check if quarantine/suspend period has expired */
    if (seq->next_sequence < domain->quarantine_until_sequence) {
        return false;
    }

    /* Domain is eligible for recovery */
    /* For quarantined domains, restart them (clear state) */
    if (domain->state == EV_DOMAIN_QUARANTINED) {
        domain->queue_head = 0;
        domain->queue_tail = 0;
        domain->queue_count = 0;
        domain->events_remaining = domain->event_budget;
        domain->consecutive_faults = 0;
        domain->state = EV_DOMAIN_READY;
        healing->total_auto_recoveries++;
        seq->total_domains_recovered++;
        return true;
    }

    /* For suspended domains, return to ready */
    if (domain->state == EV_DOMAIN_BLOCKED) {
        domain->consecutive_faults = 0;
        domain->state = EV_DOMAIN_READY;
        healing->total_auto_recoveries++;
        return true;
    }

    return false;
}

/* ===== Integrated Audit-Heal Cycle ===== */

ev_heal_action_t ev_self_audit_heal_domain(ev_sequencer_t *seq,
                                            ev_audit_t *audit,
                                            ev_healing_t *healing,
                                            ev_domain_t *domain) {
    if (!seq || !audit || !healing || !domain) return EV_HEAL_NONE;

    /* Skip unused/terminated domains */
    if (domain->state == EV_DOMAIN_UNUSED ||
        domain->state == EV_DOMAIN_TERMINATED) {
        return EV_HEAL_NONE;
    }

    /* First, check if any quarantined/suspended domain can recover */
    if (domain->state == EV_DOMAIN_QUARANTINED ||
        domain->state == EV_DOMAIN_BLOCKED) {
        if (ev_healing_check_recovery(seq, healing, domain)) {
            return EV_HEAL_RESTART;
        }
        /* Still in quarantine — don't audit */
        return EV_HEAL_NONE;
    }

    /* Run audit checks */
    ev_audit_result_t result = ev_audit_check_domain(seq, audit, domain);

    /* Apply healing based on audit result */
    ev_heal_action_t action = ev_healing_apply(seq, healing, domain, result);

    return action;
}

ev_heal_action_t ev_self_audit_heal_system(ev_sequencer_t *seq,
                                            ev_audit_t *audit,
                                            ev_healing_t *healing) {
    if (!seq || !audit || !healing) return EV_HEAL_NONE;

    ev_heal_action_t worst_action = EV_HEAL_NONE;

    for (uint32_t i = 0; i < EV_MAX_DOMAINS; i++) {
        ev_domain_t *d = &seq->domains[i];
        if (d->state == EV_DOMAIN_UNUSED ||
            d->state == EV_DOMAIN_TERMINATED) {
            continue;
        }

        ev_heal_action_t action = ev_self_audit_heal_domain(seq, audit,
                                                             healing, d);
        if (action > worst_action) {
            worst_action = action;
        }
    }

    return worst_action;
}
