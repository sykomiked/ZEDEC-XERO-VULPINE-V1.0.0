/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* navport.h — naval ports: pegging two or more units of value through a
 * shared, V-Bill funded "Sicilian account" and a perpetual cycle of mutual
 * floating-voucher exchange (a "socket").
 *
 * OWNER SPEC (2026-10-15, paraphrased): floating vouchers are the glue that
 * lets V-Bills sit beside a CBDC, a fiat system, a commodity or any other
 * rail and bridge them. Two or more parties link by exchanging floating
 * vouchers mutually in a cycle that oscillates for ever: that is a socket,
 * and it makes a "naval port", a peg between their units built on the
 * mutual exchange of V-Bills. A port has three accounts per party: the
 * usual nostro and vostro, and a "Sicilian account" of V-Bills that is the
 * mediator holding the loop together. It is decentralised, paraconsistent,
 * and takes more than two parties. Follow-up (01:38Z): V-Bills locked in a
 * port earn a "pipe yield" from the V-Bill dividend, and the 0.08889%
 * assurance fee applies to every pipe transaction.
 *
 * WHAT A PEG REALLY NEEDS. A peg holds exactly as long as the issuer of the
 * weak unit can pay what holders bring back at the peg. Here that payment
 * is V-Bills from the issuer's share of the Sicilian account, so the share
 * IS the reserve. Gross flows do not need reserves (they cancel), only the
 * net position at each close does. A persistent one-way flow out of a unit
 * drains its issuer's share; when the share is gone the port refuses
 * further outflow, and that refusal is the de-peg. Nothing in this module
 * lends to a weak member or spends another member's share to defend it.
 *
 *   N1 MEMBERS. Up to NAVP_MAX_MEMBERS parties, each an issuer of its own
 *      unit (a CBDC, fiat, commodity or compute voucher) with a peg num/den:
 *      num minor units of its unit per den V-Bill minor units. Every cross
 *      rate is derived through the V-Bill, so with K members there is one
 *      peg vector and no triangular arbitrage between three pegs.
 *   N2 SICILIAN ACCOUNT. One pooled V-Bill account per port; each member
 *      holds a share S_i of it. Deposits and withdrawals are the caller's
 *      custodian-attested V-Bill movements; this module counts them.
 *      A withdrawal must leave S_i >= min_stake + the member's current net
 *      debit. Total V-Bills (shares + returned on exit) are conserved.
 *   N3 SOCKET (navp_convert). A customer of member f hands x of unit f and
 *      receives unit t. The fee is pay_assure_fee(x) (0.08889%, taken
 *      first), v = floor((x - fee) * den_f / num_f) V-Bill value, y =
 *      floor(v * num_t / den_t) of unit t, and x_used = ceil(v * num_f /
 *      den_f) is charged (the rest stays with the customer). Member t now
 *      holds x_used of f's vouchers: its NOSTRO at f, which is f's VOSTRO
 *      for t. Each holding carries its V-Bill value fixed at conversion, so
 *      a later peg change never revalues an open position.
 *   N4 PORT LIMIT. A conversion out of unit f is refused (NAVP_E_LIMIT)
 *      if f's net debit afterwards, L_f - C_f (vouchers of f held by others
 *      minus f's holdings of others, in V-Bill value), would exceed S_f.
 *      So every close can always be paid: the peg is fully backed in net.
 *   N5 PEG HEALTH (four-valued). Each cycle, price evidence about unit f
 *      arrives as on-peg and off-peg votes (signed oracle quotes in
 *      production; counts supplied by the caller here). TRUE (only on),
 *      FALSE (only off), BOTH (conflicting), NEITHER (no fresh evidence).
 *      Outflow from f: TRUE allowed; NEITHER allowed up to stale_cap_bps
 *      of S_f per cycle (NAVP_E_STALE beyond); FALSE refused (NAVP_E_DEPEG)
 *      in SUSPEND mode, allowed in DEFEND mode (still bound by N4); BOTH
 *      held (NAVP_E_HELD) for the quorum. Inflow into f is never blocked:
 *      it helps the peg. Evidence resets to NEITHER at every close.
 *   N6 BOOKS (paraconsistent reconciliation). Each member reports its
 *      nostro row and vostro column. For the holding of i in j's vouchers
 *      the witnesses are i's nostro, j's vostro and the port state: equal
 *      reports are evidence for, unequal against. The pair is TRUE (some
 *      for, none against), FALSE, BOTH or NEITHER. Only TRUE pairs (and
 *      empty pairs) settle at close; the rest carry over untouched, so one
 *      disputed pair never blocks or corrupts the others.
 *   N7 CLOSE (navp_close_prepare / vote / commit). Prepare nets the
 *      included pairs multilaterally: n_i = claims_i - liabilities_i, sum
 *      zero exactly. If any debtor's net exceeds its share (possible only
 *      when an excluded pair held an offsetting claim) prepare refuses
 *      (NAVP_E_SHORT). The settlement is hashed (SHA-256); members sign
 *      "zxv/navp-close/v1" || port || cycle || hash. A vote for another
 *      hash is a conflict. Commit needs quorum agreeing votes (default
 *      floor(2n/3) + 1 of active members) and NO conflict (else BOTH:
 *      NAVP_E_CONFLICT, nothing moves). Commit moves V-Bills between shares
 *      (S_i += n_i), retires the settled vouchers, applies ratified
 *      proposals, finishes exits and opens the next cycle. Gross and net
 *      are recorded: compression = gross / net is netting, measured, never
 *      a multiplier of value.
 *   N8 PROPOSALS. A peg reset needs the issuing member's signature AND a
 *      quorum; an admission needs a quorum. Both apply at the next commit,
 *      after that cycle's positions settled at the old pegs. An exit needs
 *      only the member's own signature: it stops new conversions into and
 *      out of its unit, and at the first commit with nothing carried the
 *      member's whole share is returned to it.
 *   N9 PIPE YIELD (OFF by default). navp_yield splits a realised amount P
 *      the caller has taken from the 25% V-Bill dividend pool (pay_assure
 *      B1) across members, by units u_i = S_i + fee_weight * fee_v_i, where
 *      fee_v_i is the V-Bill value of fees paid on outflows since the last
 *      split. Largest remainders, ties to the lower index; sum == P. It
 *      never creates V-Bills and has no rate. Compression earns NOTHING by
 *      itself: gross volume can be faked by two members trading back and
 *      forth, and fees are the only cost of doing that. Paying holders for
 *      locking value is the same securities risk raised for GPU farming:
 *      the switch stays off until counsel says otherwise.
 *
 * HONEST LIMITS. Test units only: there is no protocol fiat on-ramp or
 * off-ramp, and each member's unit is a claim its own custodian attests.
 * Issuing redeemable vouchers, running a peg and holding V-Bills on users'
 * behalf are regulated activities (money transmission, e-money, securities)
 * that need licences: every point here is pending counsel, and V-Bills and
 * ports belong to the INSTITUTION profile. A V-Bill is worth what its
 * registered, haircut receipts and reserve are worth (kernel/src/vauction,
 * kernel/src/mint), not "every commodity ever traded". No interest, no
 * lending: a member that cannot pay its net debit is simply stopped.
 * Integer only. */
#ifndef ZXV_NAVPORT_H
#define ZXV_NAVPORT_H

#include <stdint.h>
#include <stdbool.h>

#define NAVP_MAX_MEMBERS   8u
#define NAVP_MAX_PROPOSALS 8u
#define NAVP_CODE_MAX      12u
#define NAVP_AMOUNT_MAX    ((uint64_t) 1 << 60)
#define NAVP_CLOSE_MSG     (17u + 4u + 8u + 32u)
#define NAVP_PROP_MSG      (16u + 4u + 4u + 32u)
#define NAVP_EXIT_MSG      (16u + 4u + 4u)

typedef enum {
    NAVP_OK = 0,
    NAVP_E_ARG = -1,
    NAVP_E_STATE = -2, /* wrong port, member or proposal state       */
    NAVP_E_FULL = -3,
    NAVP_E_DUST = -4,  /* the conversion is worth 0 of a unit        */
    NAVP_E_LIMIT = -5, /* N4: net debit would exceed the share       */
    NAVP_E_DEPEG = -6, /* N5: evidence says off peg (SUSPEND mode)   */
    NAVP_E_HELD = -7,  /* N5: conflicting evidence, held for quorum  */
    NAVP_E_STALE = -8, /* N5: no fresh evidence, stale cap reached   */
    NAVP_E_SIG = -9,
    NAVP_E_DUP = -10,      /* vote already counted                       */
    NAVP_E_QUORUM = -11,   /* N7: fewer agreeing votes than the quorum   */
    NAVP_E_CONFLICT = -12, /* N7: a vote for a different settlement      */
    NAVP_E_SHORT = -13,    /* N7: a debtor's share cannot pay its net    */
    NAVP_E_OVERFLOW = -14,
    NAVP_E_DISABLED = -15, /* N9: pipe yield is off                      */
    NAVP_E_UNKNOWN = -16   /* unknown member or proposal                 */
} navp_status_t;

/* Four-valued truth (same order as lpres_state_t). */
typedef enum { NAVP_NEITHER = 0, NAVP_TRUE = 1, NAVP_FALSE = 2, NAVP_BOTH = 3 } navp_lp_t;

typedef enum { NAVP_SUSPEND = 0, NAVP_DEFEND = 1 } navp_mode_t;

typedef enum {
    NAVP_M_FREE = 0,
    NAVP_M_ACTIVE = 1,
    NAVP_M_EXITING = 2,
    NAVP_M_EXITED = 3
} navp_mstate_t;

typedef enum { NAVP_OPEN = 0, NAVP_CLOSING = 1 } navp_pstate_t;

typedef enum { NAVP_PROP_PEG = 1, NAVP_PROP_ADMIT = 2 } navp_prop_kind_t;

/* verify(ctx, key, msg, len, sig, sig_len): true when sig is key's (ML-DSA
 * in production; the same shape as sic_verify_fn). */
typedef bool (*navp_verify_fn)(void *ctx, uint32_t key, const uint8_t *msg, uint32_t len,
                               const uint8_t *sig, uint32_t sig_len);

typedef struct {
    navp_mode_t mode;       /* N5 behaviour on FALSE, default SUSPEND           */
    uint32_t stale_cap_bps; /* N5 outflow allowed per cycle on NEITHER (of S_f) */
    uint64_t min_stake;     /* N2 floor of a share while a member is active     */
    uint32_t quorum;        /* N7 0 = floor(2n/3) + 1 of active members         */
    bool yield_enabled;     /* N9 off by default                                 */
    uint32_t fee_weight;    /* N9 units per V-Bill of fee paid                   */
} navp_config_t;

typedef struct {
    uint32_t key;
    char code[NAVP_CODE_MAX + 1];
    uint64_t num, den; /* peg: num minor units of this unit per den V-Bill units */
    uint64_t share;    /* S_i: this member's share of the Sicilian account      */
    uint8_t state;     /* navp_mstate_t */
    uint32_t on_votes, off_votes;
    uint64_t stale_used; /* V-Bill value moved out on NEITHER this cycle */
    uint64_t fee_due;    /* fees on outflows, in this member's unit      */
    uint64_t fee_v;      /* the same in V-Bill value, since the last yield */
    uint64_t returned;   /* V-Bills returned on exit                      */
} navp_member_t;

typedef struct {
    uint32_t id;
    uint8_t kind;   /* navp_prop_kind_t, 0 = free slot */
    uint8_t member; /* PEG: the member; ADMIT: the new slot */
    uint8_t ratified;
    uint8_t approved; /* bitmask of member indices */
    uint32_t key;     /* ADMIT */
    char code[NAVP_CODE_MAX + 1];
    uint64_t num, den, stake;
    uint8_t digest[32];
} navp_proposal_t;

typedef struct {
    uint64_t fee, x_used, v, y, fee_v;
} navp_quote_t;

typedef struct {
    uint32_t port_id;
    navp_config_t cfg;
    navp_verify_fn verify;
    void *vctx;
    uint32_t n; /* member slots used */
    navp_member_t m[NAVP_MAX_MEMBERS];
    /* hold[i][j]: vouchers of member j's unit held by member i (i's nostro
     * at j = j's vostro for i), in j's minor units; val[i][j] its V-Bill value. */
    uint64_t hold[NAVP_MAX_MEMBERS][NAVP_MAX_MEMBERS];
    uint64_t val[NAVP_MAX_MEMBERS][NAVP_MAX_MEMBERS];
    uint64_t nostro_rep[NAVP_MAX_MEMBERS][NAVP_MAX_MEMBERS];
    uint64_t vostro_rep[NAVP_MAX_MEMBERS][NAVP_MAX_MEMBERS];
    uint8_t reported; /* bitmask */
    uint8_t pstate;   /* navp_pstate_t */
    uint64_t cycle;
    /* the prepared close */
    uint8_t tag[NAVP_MAX_MEMBERS][NAVP_MAX_MEMBERS]; /* navp_lp_t */
    int64_t net[NAVP_MAX_MEMBERS];
    uint8_t hash[32];
    uint8_t agree, conflict; /* bitmasks of votes */
    uint64_t gross, settled; /* of the prepared close */
    /* history */
    uint64_t total_gross, total_settled, closes;
    uint64_t genesis, deposits, withdrawals; /* V-Bill conservation */
    bool started;                            /* a conversion happened: no genesis members after */
    navp_proposal_t prop[NAVP_MAX_PROPOSALS];
    uint32_t next_prop;
} navp_t;

void navp_config_default(navp_config_t *c);
navp_status_t navp_init(navp_t *p, uint32_t port_id, const navp_config_t *cfg,
                        navp_verify_fn verify, void *vctx);
const char *navp_status_name(navp_status_t s);

/* Genesis members, before the first conversion. stake >= min_stake. */
navp_status_t navp_member_add(navp_t *p, uint32_t key, const char *code, uint64_t num, uint64_t den,
                              uint64_t stake, uint32_t *out_idx);
navp_status_t navp_deposit(navp_t *p, uint32_t m, uint64_t vbills);
navp_status_t navp_withdraw(navp_t *p, uint32_t m, uint64_t vbills);

/* N5: add on-peg / off-peg evidence for this cycle; returns the state. */
navp_lp_t navp_evidence(navp_t *p, uint32_t m, uint32_t on, uint32_t off);
navp_lp_t navp_health(const navp_t *p, uint32_t m);

/* N3 */
navp_status_t navp_quote(const navp_t *p, uint32_t from, uint32_t to, uint64_t x, navp_quote_t *q);
navp_status_t navp_convert(navp_t *p, uint32_t from, uint32_t to, uint64_t x, navp_quote_t *q);

/* liabilities (others' holdings of m) and claims (m's holdings), V-Bill value */
void navp_position(const navp_t *p, uint32_t m, uint64_t *liab, uint64_t *claim);

/* N6: nostro[j] = what m holds of j; vostro[j] = what j holds of m. */
navp_status_t navp_books_report(navp_t *p, uint32_t m, const uint64_t nostro[NAVP_MAX_MEMBERS],
                                const uint64_t vostro[NAVP_MAX_MEMBERS]);

/* N7 */
navp_status_t navp_close_prepare(navp_t *p);
void navp_close_msg(uint32_t port_id, uint64_t cycle, const uint8_t hash[32],
                    uint8_t out[NAVP_CLOSE_MSG]);
navp_status_t navp_close_vote(navp_t *p, uint32_t m, const uint8_t hash[32], const uint8_t *sig,
                              uint32_t sig_len);
navp_status_t navp_close_commit(navp_t *p);
navp_status_t navp_close_abort(navp_t *p);
uint32_t navp_quorum(const navp_t *p);

/* N8 */
navp_status_t navp_propose_peg(navp_t *p, uint32_t m, uint64_t num, uint64_t den, uint32_t *out_id);
navp_status_t navp_propose_admit(navp_t *p, uint32_t key, const char *code, uint64_t num,
                                 uint64_t den, uint64_t stake, uint32_t *out_id);
/* "zxv/navp-prop/v1" || port || id || digest */
navp_status_t navp_prop_msg(const navp_t *p, uint32_t id, uint8_t out[NAVP_PROP_MSG]);
navp_status_t navp_approve(navp_t *p, uint32_t id, uint32_t m, const uint8_t *sig,
                           uint32_t sig_len);
/* "zxv/navp-exit/v1" || port || member */
void navp_exit_msg(uint32_t port_id, uint32_t m, uint8_t out[NAVP_EXIT_MSG]);
navp_status_t navp_exit(navp_t *p, uint32_t m, const uint8_t *sig, uint32_t sig_len);

/* N9: shares[i] for every member slot; sum == pool. */
navp_status_t navp_yield(navp_t *p, uint64_t pool, uint64_t shares[NAVP_MAX_MEMBERS]);

/* Invariants: V-Bill conservation, every active member within N4, holdings
 * and values zero together. */
bool navp_check(const navp_t *p);

#endif
