/* curzi_tier.c — CURZI-8889-A LAYER 5: hierarchical tiered access.
 *
 * Contract: kernel/src/curzi/curzi8889a.h (authoritative; not modified here).
 *
 * WHAT THIS BUYS, AND WHY IT IS THE WHOLE P2P STORY
 * -------------------------------------------------
 * A peer holding the key at (domain d, level L) derives every level ABOVE L in
 * d, and nothing below it, and nothing at all in another domain. Content is
 * chunked and each chunk sealed under its own tier key, so a peer can STORE and
 * SERVE ciphertext it cannot read. Sharing bandwidth stops implying sharing
 * plaintext. A grant is 32 bytes and nothing else -- no certificate, no list,
 * no server to ask.
 *
 * THE ONE-WAY CHAIN
 * -----------------
 *   domain root      DR(d)   = H( system_root || "CURZI-8889-A/domain" || d_le16 )
 *   level 1          K(d,1)  = H( DR(d)       || "CURZI-8889-A/level"  || d_le16 || 1 )
 *   level L+1        K(d,L+1)= H( K(d,L)      || "CURZI-8889-A/level"  || d_le16 || L+1 )
 *
 * H is SHA3-256 (FIPS 202) from kernel/src/mlkem/keccak.h. It is not
 * reimplemented here: that file is verified against the NIST ACVP vectors by
 * test_mlkem_kat.c, and a second copy of a hash is a second thing that can be
 * silently wrong. (keccak.c:31-49 records a shift-by-64 UB that made EVERY
 * SHA-3 output in this system wrong while remaining self-consistent -- fixed
 * 2026-08-12. That is the exact failure mode a private copy would reintroduce.)
 *
 * DESCENT IS IMPOSSIBLE TWICE OVER, WHICH IS THE POINT
 * ----------------------------------------------------
 *   1. COMPUTATIONALLY. Recovering K(d,L) from K(d,L+1) is a SHA3-256 preimage
 *      on a 53-byte message of which 32 bytes are the unknown. There is no
 *      shortcut that is not a break of SHA-3 itself.
 *   2. STRUCTURALLY. The API refuses to be asked: to_level < from_level returns
 *      CURZI_E_TIER_DESCEND and writes nothing usable. Relying on (1) alone
 *      would mean a caller with a sign error silently gets a WRONG key and
 *      treats it as right; the refusal turns that into a reported error.
 * Contract falsification test (curzi8889a.h:126): "if a tier key at level L can
 * derive a key at level < L, LAYER 5 is void." test_curzi_tier.c checks both
 * the refusal and, empirically, that the forward closure of K(d,L) contains no
 * key at a lower level and no key of any other domain.
 *
 * LEVEL NUMBERING: WHY INDEX 0 IS THE DOMAIN ROOT
 * -----------------------------------------------
 * curzi8889a.h:104-107 counts "61 LEVELS per domain, PLUS a domain root", so
 * the domain root is a distinct object from the 61 level keys -- yet
 * curzi_tier_derive's only handle on "where the parent sits" is one uint8_t.
 * If level 0 were a level key there would be no way to say "my parent is the
 * domain root" without inventing a sentinel, and a sentinel is a chosen magic
 * number in exactly the place the standard's no-backdoor argument says not to
 * put one (curzi8889a.h:44-47).
 *
 * So level index 0 IS the domain root position, and the 61 grantable levels are
 * 1..61 == 1..CURZI_LEVELS. This reuses a convention the contract already
 * states one layer down: CURZI_E_SHARE_INDEX, "share index 0 is the secret
 * itself; forbidden". Index 0 is the root in both layers. Nothing is invented.
 *
 *   valid domain      : 0 .. CURZI_DOMAINS-1   (0..143)
 *   valid level index : 0 .. CURZI_LEVELS      (0..61), 0 == domain root
 *   grantable levels  : 1 .. CURZI_LEVELS      (61 per domain)
 *
 * CONSTANT TIME -- WHAT IS ACHIEVED AND WHAT IS NOT
 * -------------------------------------------------
 * SECRET here means: system_root, parent_key, out_key, and every intermediate
 * chain value. PUBLIC means: domain, from_level, to_level -- these are the
 * ADDRESS of a grant, travel in the clear beside the sealed chunk, and are what
 * a peer indexes its store by.
 *
 *   ACHIEVED. No branch, no loop bound, no array index and no comparison in
 *   this file depends on a single bit of secret material. Secrets are only ever
 *   copied by fixed-count loops and fed to sha3_256 over a COMPILE-TIME
 *   CONSTANT length, so the sponge's absorb/squeeze path is fixed too.
 *   Keccak-f[1600] is itself branch-free and table-free on the message: its one
 *   table (round constants) is indexed by round number (keccak.c:90).
 *
 *   NOT ACHIEVED, stated rather than hidden. The number of hash invocations is
 *   (to_level - from_level), so the WALK LENGTH is observable by timing. That
 *   is public metadata under the threat model above. If a deployment ever
 *   treats the tier address itself as secret, this file does not hide it and
 *   must not be claimed to.
 *
 *   Also not achieved, and not achievable here: the caller's own control flow.
 *   A caller that branches on the returned key is outside this file's reach.
 *
 * FAIL CLOSED. Every error path zeroes out_key before returning, so a caller
 * that ignores the return value gets 32 zero bytes rather than stale stack or a
 * partially-walked chain. The chain is walked in a local and copied out exactly
 * once, on success only.
 *
 * NO MODULE DECLARATION IN THIS TU. ZXV_DECLARE names a MODULE; CURZI-8889-A is
 * one module spread over several translation units, so the declaration belongs
 * to the composite's top-level TU (the one defining curzi8889a_selfcheck), not
 * to each layer. Three declarations for one subsystem would put three nodes in
 * the modbind graph where the system has one.
 *
 * Freestanding: integer only, no libc, no allocation, no float, no 64-bit
 * variable divide.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (CURZI-8889-A LAYER 5 slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "curzi8889a.h"
#include "../mlkem/keccak.h"

/* ---- domain-separation labels -------------------------------------------
 * Written as ASCII string literals, not byte arrays, so a reader sees the exact
 * bytes that are hashed without decoding hex. They are the standard's own name
 * plus a path segment: nothing here was chosen for its value, which is the
 * whole "no unexplained constant" argument (curzi8889a.h:44-47).
 *
 * The two labels differ in content AND in length, and every field around them
 * is fixed width, so no input to one function can ever be re-read as an input
 * to the other. See the injectivity note on the buffer layouts below. */
static const char CURZI_LBL_DOMAIN[] = "CURZI-8889-A/domain";
static const char CURZI_LBL_LEVEL[]  = "CURZI-8889-A/level";

#define LBL_DOMAIN_LEN  (sizeof CURZI_LBL_DOMAIN - 1u)   /* 19, no NUL hashed */
#define LBL_LEVEL_LEN   (sizeof CURZI_LBL_LEVEL  - 1u)   /* 18, no NUL hashed */

#define CURZI_KEY_BYTES 32u                              /* == SHA3_256_DIGEST_LEN */

/* Preimage layouts. Every field is FIXED WIDTH, so the concatenation is
 * injective: there is exactly one (key, domain, level) that produces a given
 * buffer, and no length-prefix is needed to make it unambiguous.
 *
 *   domain: [0..31] parent(system root) | [32..50] label(19) | [51..52] d_le16
 *   level : [0..31] parent              | [32..49] label(18) | [50..51] d_le16 | [52] level
 *
 * Both happen to be 53 bytes; that is a coincidence, not a requirement, so the
 * two sizes are computed independently rather than shared. */
#define BUF_DOMAIN_LEN  (CURZI_KEY_BYTES + LBL_DOMAIN_LEN + 2u)
#define BUF_LEVEL_LEN   (CURZI_KEY_BYTES + LBL_LEVEL_LEN  + 3u)

_Static_assert(CURZI_KEY_BYTES == SHA3_256_DIGEST_LEN,
               "tier keys are exactly one SHA3-256 digest");
_Static_assert(CURZI_LEVELS <= 255u,
               "level index must fit the uint8_t in the contract's signature");
_Static_assert(CURZI_DOMAINS <= 0xFFFFu,
               "domain index must fit the uint16_t in the contract's signature");

/* ---- tiny fixed-count primitives (no libc in a freestanding kernel) ------
 * Both are counted loops over PUBLIC lengths and touch secret bytes only as
 * opaque data, so neither can leak through timing. */
static void tier_copy(uint8_t *dst, const uint8_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

/* `volatile` is load-bearing: a plain zeroing loop over a buffer that is never
 * read again is a dead store, and -O2 is entitled to delete it -- which would
 * leave chain keys on the kernel stack. There is no memset_s to call here. */
static void tier_zero(uint8_t *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    for (size_t i = 0; i < n; i++) v[i] = 0;
}

/* Little-endian, explicitly, byte by byte. The kernel ships for five
 * architectures including 32-bit ones; hashing a struct or a cast uint16_t
 * would make the derived key depend on the target's byte order, and a grant
 * issued on one arch would not open on another. */
static void tier_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

/* One chain step: K(d, level) from its parent. `level` is the level being
 * PRODUCED, so the same byte can never label two different keys. */
static void tier_step(uint8_t key[CURZI_KEY_BYTES], uint16_t domain, uint8_t level)
{
    uint8_t buf[BUF_LEVEL_LEN];
    size_t  o = 0;

    tier_copy(buf, key, CURZI_KEY_BYTES);                       o += CURZI_KEY_BYTES;
    tier_copy(buf + o, (const uint8_t *)CURZI_LBL_LEVEL,
              LBL_LEVEL_LEN);                                   o += LBL_LEVEL_LEN;
    tier_put_le16(buf + o, domain);                             o += 2u;
    buf[o] = level;                                             o += 1u;

    sha3_256(buf, o, key);      /* in place: the parent is consumed by its child */
    tier_zero(buf, sizeof buf); /* the parent key was in there */
}

/* ---- LAYER 5 API --------------------------------------------------------- */

curzi_err_t curzi_tier_root(const uint8_t system_root[32],
                            uint16_t domain, uint8_t out_key[32])
{
    uint8_t buf[BUF_DOMAIN_LEN];
    size_t  o = 0;

    /* Zeroed here too, not just on the range path: "fail closed" has to be
     * total, or the one error that leaves the buffer untouched becomes the one
     * a caller gets stale bytes from. */
    if (system_root == 0 || out_key == 0) {
        if (out_key != 0) tier_zero(out_key, CURZI_KEY_BYTES);
        return CURZI_E_NULL;
    }

    /* Range before anything else: an out-of-space domain is not a key that
     * happens to be wrong, it is not a key at all. Zero first so a caller that
     * ignores the return value cannot mistake stale bytes for a root. */
    if (domain >= CURZI_DOMAINS) {
        tier_zero(out_key, CURZI_KEY_BYTES);
        return CURZI_E_TIER_RANGE;
    }

    tier_copy(buf, system_root, CURZI_KEY_BYTES);               o += CURZI_KEY_BYTES;
    tier_copy(buf + o, (const uint8_t *)CURZI_LBL_DOMAIN,
              LBL_DOMAIN_LEN);                                  o += LBL_DOMAIN_LEN;
    tier_put_le16(buf + o, domain);                             o += 2u;

    sha3_256(buf, o, out_key);
    tier_zero(buf, sizeof buf);   /* held the system root: the crown jewel */
    return CURZI_OK;
}

curzi_err_t curzi_tier_derive(const uint8_t parent_key[32],
                              uint16_t domain, uint8_t from_level,
                              uint8_t to_level, uint8_t out_key[32])
{
    uint8_t k[CURZI_KEY_BYTES];

    if (parent_key == 0 || out_key == 0) {
        if (out_key != 0) tier_zero(out_key, CURZI_KEY_BYTES);
        return CURZI_E_NULL;
    }

    /* ALIASING, AND WHAT IT COSTS ON AN ERROR PATH. out_key may be the same
     * buffer as parent_key -- walking a chain in place is the obvious usage and
     * the success path supports it (the walk runs in a local, see below). The
     * consequence, stated because it is surprising: on a REFUSED call the
     * zeroing below destroys an aliased parent_key. Fail-closed wins over
     * preserving the input, because a caller that ignores the return value must
     * not be handed something that looks like a key. A caller that needs its
     * parent to survive a rejected request passes a separate out_key. */

    /* RANGE is checked BEFORE DESCEND, deliberately. When both apply (say
     * from_level 200, to_level 5) the honest report is that the address is
     * outside the declared space; calling that a descent would tell the caller
     * their direction was wrong when their coordinates do not exist. */
    if (domain >= CURZI_DOMAINS) {
        tier_zero(out_key, CURZI_KEY_BYTES);
        return CURZI_E_TIER_RANGE;
    }
    /* <= CURZI_LEVELS, not <: index 0 is the domain root and 1..61 are the 61
     * grantable levels. See "LEVEL NUMBERING" at the top of this file. */
    if (from_level > CURZI_LEVELS || to_level > CURZI_LEVELS) {
        tier_zero(out_key, CURZI_KEY_BYTES);
        return CURZI_E_TIER_RANGE;
    }
    if (to_level < from_level) {
        tier_zero(out_key, CURZI_KEY_BYTES);
        return CURZI_E_TIER_DESCEND;     /* the falsification test of LAYER 5 */
    }

    /* Walked in a local, copied out exactly once on success. A fault partway
     * through therefore cannot leave a half-walked chain in the caller's
     * buffer, and out_key may safely alias parent_key. */
    tier_copy(k, parent_key, CURZI_KEY_BYTES);

    /* to_level == from_level walks zero steps and returns what the caller
     * already holds. That is an identity, not an escalation: it grants nothing
     * the caller did not present. Refusing it would force every caller to
     * special-case its own tier. */
    for (unsigned l = from_level; l < (unsigned)to_level; l++) {
        tier_step(k, domain, (uint8_t)(l + 1u));
    }

    tier_copy(out_key, k, CURZI_KEY_BYTES);
    tier_zero(k, sizeof k);
    return CURZI_OK;
}

/* ---- THE TIER ARITHMETIC, MEASURED RATHER THAN ASSERTED ------------------
 * curzi8889a.h:165-168 says: "1 system root + 144 domain roots + 144*61 level
 * keys = 8929 addressable. 8889 of those are GRANTABLE tiers; the remainder are
 * structural roots that are never handed out."
 *
 * The first sentence is exact:  1 + 144 + 8784 = 8929.
 * The second does not close. The structural roots are the 1 system root plus
 * the 144 domain roots = 145, so grantable = 8929 - 145 = 8784, not 8889.
 * 8929 - 8889 = 40, and there is no set of 40 structural roots in this design.
 *
 * Nor is 8889 a domains x levels rectangle: 8889 = 3 * 2963 with 2963 prime, so
 * its only factorisations are 1x8889 and 3x2963 -- neither has a factor <= 255,
 * so no (domains, levels) pair expressible in this contract's uint16_t/uint8_t
 * can produce it.
 *
 * This code does NOT bend to make the comment true. The real numbers are:
 *   level keys   144 * 61 = 8784      grantable
 *   domain roots           =  144     structural
 *   system root            =    1     structural
 *   addressable            = 8929
 *   grantable              = 8784
 * CURZI_TIER_COUNT (8889) is the STANDARD'S NAME, not a count this structure
 * produces. test_curzi_tier.c recomputes every one of these numbers and prints
 * the discrepancy rather than hiding it. Changing CURZI_DOMAINS or CURZI_LEVELS
 * to chase 8889 would be choosing constants to fit a label -- the precise move
 * the no-backdoor argument forbids. */
