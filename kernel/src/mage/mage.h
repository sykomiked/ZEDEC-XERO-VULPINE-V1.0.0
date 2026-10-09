/* mage.h — The Mage's Hats: a role + authorization security framework
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * WHAT THIS IS
 * ------------
 * A security operations framework where every capability is classified by the
 * "wizard's hat" a person wears, and where the difference between a
 * CONSTRUCTIVE and a DESTRUCTIVE use of the same tool is not the tool — it is
 * an AUTHORIZATION. A port scanner pointed at your own network is diagnostics;
 * the identical scanner pointed at someone who did not consent is an attack.
 * The tool is the same. The engagement is what differs. This framework makes
 * the engagement a first-class, enforced object.
 *
 * The whole point of red/purple/black-team tooling is to test a system you are
 * authorized to test so it comes out stronger. This framework makes those
 * tools AVAILABLE — and binds them to a scoped, signed, expiring ENGAGEMENT so
 * they operate only where authorization exists. Testing your own architecture
 * is the cleanest case: you self-authorize an engagement over your own assets,
 * and the tools run.
 *
 * THE HATS (a role a person wears; one active at a time)
 * ------------------------------------------------------
 *   WHITE   authorized security professional — defense AND offense under an
 *           engagement. The full kit.
 *   BLACK   adversary emulation — the threat-actor role, used to attack a
 *           system you are authorized to test so its defenders learn. Offensive
 *           capabilities, ALWAYS under an engagement. This is not a licence to
 *           attack others; it is the role you wear to attack YOURSELF on
 *           purpose.
 *   RED     red team — offensive, adversary emulation, under an engagement.
 *   BLUE    blue team — detection, hardening, incident response. Open.
 *   PURPLE  red + blue at once — the collaborative loop where an attack is run
 *           and the defense's response is measured in the same session.
 *   GREEN   learner — every capability, but SANDBOXED: simulated targets only,
 *           never a live asset. Safe to fail.
 *   CLEAR   transparency / audit — read-only observation with a full audit
 *           trail. Sees everything, touches nothing.
 *   YELLOW  builder / AppSec — hardening and secure-configuration capabilities
 *           for the person who builds the thing. Open.
 *   ORANGE  threat-informed builder (yellow + red knowledge) — builder tools
 *           plus the adversary catalog, so defenses are designed against real
 *           techniques. The offensive EXECUTION still needs an engagement.
 *   GREY    unauthorized-but-curious — defensive/educational capabilities only;
 *           any offensive capability is DENIED and the attempt is logged. Grey
 *           is nudged toward White by acquiring an engagement, not punished.
 *
 * THE ENFORCEMENT (mage_authorize)
 * --------------------------------
 * A capability request is (hat, capability, target). The decision:
 *   - OPEN capabilities (defense, audit, passive recon) run for the hats that
 *     hold them, no engagement required.
 *   - GATED capabilities (scan, exploit, C2, lateral, exfil emulation) require
 *     an ACTIVE ENGAGEMENT whose scope covers the target, whose rules of
 *     engagement permit the capability, that has not expired, and that is
 *     authorized (self-authorized by the asset owner, or signed by a
 *     registered authorizer). No engagement -> NEEDS_ENGAGEMENT, never a
 *     silent allow.
 *   - GREEN runs everything in a sandbox -> SANDBOX_ONLY.
 *   - A hat under rehabilitative containment (see src/concord) -> CONTAINED for
 *     offensive capabilities; it keeps defensive and educational ones.
 *
 * EXPIRY IS IN PHASE TICKS, NOT A CLOCK. ZXV sequences on event-phase ordinals
 * (include/m5_types.h Cycle Pulse). An engagement's window is [from_tick,
 * until_tick]; the caller passes the current phase ordinal. Nothing here reads
 * a wall clock.
 *
 * WHAT THIS DELIBERATELY IS NOT (stated so it is not discovered later)
 * -------------------------------------------------------------------
 * This is a framework and a set of INSTRUMENTED, SCOPED harnesses. It is not,
 * and will not become, a kit for harming systems you are not authorized to
 * test. It contains no self-propagating code, no denial-of-service amplifier,
 * no mass-target scanner, and no detection-evasion-for-crime. The adversary
 * emulation (mage_emulate) does not deliver real payloads: it attempts a named
 * technique against a target within an engagement and REPORTS whether a
 * registered defense caught it or a gap exists. Its output is a finding, not a
 * foothold. That is what makes it strengthen the ecosystem instead of the
 * attacker.
 *
 * Freestanding: integer only, no libc, no allocation.
 */
#ifndef ZXV_MAGE_H
#define ZXV_MAGE_H

#include <stdint.h>
#include <stdbool.h>

#define MAGE_MAX_ENGAGEMENTS 16u
#define MAGE_MAX_SCOPE       32u    /* assets per engagement                */
#define MAGE_MAX_ASSETS      128u   /* assets the local node knows about    */
#define MAGE_MAX_AUDIT       256u   /* hash-chained security event log      */
#define MAGE_HASH_LEN        32u

/* ---- hats ---- */
typedef enum {
    MAGE_HAT_NONE = 0,
    MAGE_HAT_WHITE,
    MAGE_HAT_BLACK,
    MAGE_HAT_RED,
    MAGE_HAT_BLUE,
    MAGE_HAT_PURPLE,
    MAGE_HAT_GREEN,
    MAGE_HAT_CLEAR,
    MAGE_HAT_YELLOW,
    MAGE_HAT_ORANGE,
    MAGE_HAT_GREY,
    MAGE_HAT__COUNT
} mage_hat_t;

/* ---- capability classes ---- */
typedef enum {
    /* OPEN — defensive, investigative, passive. No engagement required. */
    MAGE_CAP_RECON_PASSIVE = 0, /* observe already-available info about self */
    MAGE_CAP_HARDEN,            /* apply/verify defensive configuration      */
    MAGE_CAP_MONITOR,           /* watch the event stream for anomalies      */
    MAGE_CAP_DETECT,            /* run detections                            */
    MAGE_CAP_RESPOND,           /* contain / isolate / recover               */
    MAGE_CAP_FORENSIC,          /* reconstruct a timeline                    */
    MAGE_CAP_AUDIT,             /* read-only inspection                      */
    MAGE_CAP_EDUCATE,           /* sandboxed practice                        */
    /* GATED — active/offensive. Require a scoped engagement. */
    MAGE_CAP_SCAN,              /* active probing / enumeration of a target  */
    MAGE_CAP_EXPLOIT,           /* gain access — adversary emulation         */
    MAGE_CAP_C2,                /* command & control / persistence emulation */
    MAGE_CAP_LATERAL,           /* lateral movement / escalation emulation   */
    MAGE_CAP_EXFIL,             /* data-exfiltration emulation               */
    MAGE_CAP__COUNT
} mage_cap_t;

/* ---- decision ---- */
typedef enum {
    MAGE_ALLOW = 0,        /* run it                                        */
    MAGE_DENY,             /* this hat may never do this                    */
    MAGE_NEEDS_ENGAGEMENT, /* offensive cap with no covering engagement     */
    MAGE_SANDBOX_ONLY,     /* green: run against a simulated target only     */
    MAGE_CONTAINED,        /* hat is under rehabilitative containment        */
    MAGE_EXPIRED,          /* an engagement existed but its window has passed*/
    MAGE_OUT_OF_SCOPE      /* engagement exists but does not cover this asset*/
} mage_decision_t;

/* An asset the local node is responsible for or is testing. Identified by a
 * 64-bit id (in practice a hash of a stable name). "owned" marks assets this
 * node/operator controls — those can be self-authorized for testing. */
typedef struct {
    uint64_t id;
    bool     in_use;
    bool     owned;        /* we control it -> may self-authorize testing   */
    uint8_t  baseline[MAGE_HASH_LEN];  /* integrity baseline, 0 if unset    */
    bool     have_baseline;
} mage_asset_t;

/* Rules of engagement: which capabilities are permitted, as a bitmask over
 * mage_cap_t. */
typedef uint32_t mage_roe_t;
#define MAGE_ROE_BIT(cap) (1u << (cap))

/* A scoped, time-boxed authorization to exercise gated capabilities against a
 * specific set of assets. */
typedef struct {
    uint32_t   id;
    bool       in_use;
    bool       authorized;      /* self-authorized OR signature verified     */
    uint64_t   scope[MAGE_MAX_SCOPE];
    uint32_t   scope_len;
    mage_roe_t roe;             /* permitted capabilities                    */
    uint64_t   from_tick;       /* window start (phase ordinal)              */
    uint64_t   until_tick;      /* window end                                */
    uint8_t    authorizer[32];  /* pubkey of who authorized it               */
} mage_engagement_t;

/* One hash-chained security-audit entry. Tampering breaks the chain. */
typedef struct {
    uint64_t seq;
    uint8_t  hat;
    uint8_t  cap;
    uint8_t  decision;
    uint64_t target;
    uint64_t tick;
    uint8_t  prev[MAGE_HASH_LEN];
    uint8_t  hash[MAGE_HASH_LEN];
} mage_audit_entry_t;

/* Signature verifier hook (injectable, like immigration/count_house). When
 * unset, an engagement can still be authorized by SELF-AUTHORIZATION over
 * owned assets, but a signed external authorization cannot be accepted. */
typedef bool (*mage_verify_fn)(const uint8_t *msg, uint32_t len,
                               const uint8_t sig[64], const uint8_t pubkey[32]);

typedef struct {
    mage_asset_t       assets[MAGE_MAX_ASSETS];
    uint32_t           asset_count;
    mage_engagement_t  engagements[MAGE_MAX_ENGAGEMENTS];
    uint32_t           engagement_count;

    mage_audit_entry_t audit[MAGE_MAX_AUDIT];
    uint32_t           audit_count;   /* total ever appended (may exceed cap)*/
    uint32_t           audit_head;    /* ring index of next write            */

    /* per-hat containment (rehabilitative quarantine, cf. src/concord) */
    bool               contained[MAGE_HAT__COUNT];

    mage_verify_fn     verify;
} mage_ctx_t;

/* ===================== lifecycle ===================== */

void mage_init(mage_ctx_t *m);
void mage_set_verifier(mage_ctx_t *m, mage_verify_fn fn);

/* Register an asset. `owned` marks it as ours (self-authorizable for testing).
 * Returns the asset id, or 0 on failure. */
uint64_t mage_asset_add(mage_ctx_t *m, uint64_t id, bool owned);
mage_asset_t *mage_asset_get(mage_ctx_t *m, uint64_t id);

/* ===================== engagements ===================== */

/* Self-authorize an engagement over assets we OWN. This is the "test my own
 * architecture" path: every asset in scope must be registered and owned, or
 * the engagement is refused. Returns engagement id, 0 on failure. */
uint32_t mage_engage_self(mage_ctx_t *m,
                          const uint64_t *scope, uint32_t scope_len,
                          mage_roe_t roe,
                          uint64_t from_tick, uint64_t until_tick);

/* Authorize an engagement with an external signature over its scope+roe+window.
 * Requires a verifier to be set and the signature to verify against `authorizer`.
 * Returns engagement id, 0 on failure. */
uint32_t mage_engage_signed(mage_ctx_t *m,
                            const uint64_t *scope, uint32_t scope_len,
                            mage_roe_t roe,
                            uint64_t from_tick, uint64_t until_tick,
                            const uint8_t authorizer[32],
                            const uint8_t sig[64]);

/* ===================== the decision ===================== */

/* The core gate. Given the hat a person wears, the capability they invoke, the
 * target asset, and the current phase tick, decide — and record the decision
 * in the audit chain. This is the ONE function every capability must call
 * before acting. */
mage_decision_t mage_authorize(mage_ctx_t *m, mage_hat_t hat, mage_cap_t cap,
                               uint64_t target, uint64_t now_tick);

/* True if a capability is OPEN (no engagement) rather than GATED. */
bool mage_cap_is_gated(mage_cap_t cap);

/* The set of capabilities a hat may EVER exercise (before engagement checks),
 * as a bitmask. Used for UI and for the grey-hat denial path. */
mage_roe_t mage_hat_grants(mage_hat_t hat);

/* Place / lift rehabilitative containment on a hat (cf. src/concord). A
 * contained hat keeps defensive and educational capabilities. */
void mage_contain(mage_ctx_t *m, mage_hat_t hat, bool on);

/* ===================== the audit chain ===================== */

/* Verify the whole audit chain: returns true if every entry's hash matches
 * H(entry-fields || prev), i.e. nothing was altered or removed. */
bool mage_audit_verify(const mage_ctx_t *m);
uint32_t mage_audit_len(const mage_ctx_t *m);

/* ===================== defensive tools (real, now) ===================== */

/* Integrity: record a baseline hash for an asset, and later check it. The
 * check returns true if the content still matches the baseline. This is the
 * defender's tamper detector; a red/black engagement's job is to try to change
 * something without this catching it. */
bool mage_baseline_set(mage_ctx_t *m, uint64_t asset, const uint8_t *content, uint32_t len);
bool mage_integrity_check(mage_ctx_t *m, uint64_t asset, const uint8_t *content, uint32_t len);

/* Anomaly detection over a stream of counts: given a rolling window, flag when
 * a value exceeds mean + k*stddev-ish (integer, no float division-by-tiny).
 * Returns true when the sample is anomalous. Simple, honest, testable. */
typedef struct { uint32_t win[16]; uint32_t n; uint32_t sum; } mage_rate_t;
void mage_rate_init(mage_rate_t *r);
bool mage_rate_observe(mage_rate_t *r, uint32_t sample, uint32_t k_times_4);

/* ===================== adversary emulation (constructive) ===================== */

/* A named technique the emulator can attempt. These map to real adversary
 * behaviour so that running them against YOUR OWN system exercises YOUR OWN
 * defenses. None of them delivers a real payload; each attempts the behaviour
 * in an instrumented way and the result says whether a defense responded. */
typedef enum {
    MAGE_TECH_PORT_PROBE = 0,   /* is a service reachable that should not be? */
    MAGE_TECH_AUTH_GUESS,       /* does auth resist repeated attempts?        */
    MAGE_TECH_TAMPER,           /* can content change without integrity catch?*/
    MAGE_TECH_REPLAY,           /* is a captured token accepted twice?        */
    MAGE_TECH_INJECT,           /* is untrusted input reflected into a sink?  */
    MAGE_TECH_EXFIL_CHANNEL,    /* can data leave over an unmonitored path?   */
    MAGE_TECH__COUNT
} mage_technique_t;

typedef enum {
    MAGE_FINDING_CAUGHT = 0,    /* a defense detected/blocked it — good       */
    MAGE_FINDING_GAP,           /* it succeeded and nothing noticed — a weakness*/
    MAGE_FINDING_NOT_APPLICABLE /* technique does not apply to this target     */
} mage_finding_t;

/* A defense the target has registered against a technique. Returns true if the
 * defense CATCHES the emulated technique. The emulator calls every registered
 * defense for the technique; if any catches it, the finding is CAUGHT. */
typedef bool (*mage_defense_fn)(mage_technique_t tech, uint64_t target, void *ctx);

/* Register a defense for a technique. Up to MAGE_MAX_DEFENSES per technique. */
#define MAGE_MAX_DEFENSES 8u
bool mage_defense_register(mage_ctx_t *m, mage_technique_t tech,
                           mage_defense_fn fn, void *ctx);

/* Run a technique against a target, WITHIN an engagement. Returns the finding.
 * Refuses (MAGE_FINDING_NOT_APPLICABLE) and logs a DENY if the hat/engagement
 * does not authorize the underlying capability against the target — so the
 * emulator cannot be used to reach an unauthorized target. Every run is
 * recorded in the audit chain. This is the purple-team loop in one call. */
mage_finding_t mage_emulate(mage_ctx_t *m, mage_hat_t hat,
                            mage_technique_t tech, uint64_t target,
                            uint64_t now_tick);

/* Map a technique to the capability it exercises (for the authorization check). */
mage_cap_t mage_technique_cap(mage_technique_t tech);

/* Clear the file-static defense registry (tests and re-initialisation). */
void mage_defenses_reset(void);

#endif /* ZXV_MAGE_H */
