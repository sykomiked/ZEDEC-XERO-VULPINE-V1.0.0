/* abupdate.c — A/B signed-package update with probation + auto-rollback.
 * See abupdate.h. Every mutation is a single journaled ZXVFS write, so a
 * crash leaves the system in a consistent A-or-B state.
 */
#include "abupdate.h"
#include "zsp.h"

static const char *SLOT_FILE[2] = { "app.slotA", "app.slotB" };
static const char *STATE_FILE = "app.state";

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static bool is_zsp2(const uint8_t *h, uint32_t n)
{
    return n >= 4 && h[0] == ZSP_MAGIC0 && h[1] == ZSP_MAGIC1 && h[2] == ZSP_MAGIC2 &&
           h[3] == ZSP2_MAGIC3;
}

/* Strict monotonicity (owner decision): a package may be staged or promoted
 * only if its signed version is ABOVE the active slot's version and at or above
 * the persisted anti-rollback floor. */
static bool version_allowed(const ab_state_t *st, uint32_t v)
{
    return v > st->version[st->active_slot] && v >= st->rollback_floor;
}

static ab_result_t save_state(zxvfs_t *fs, const ab_state_t *st) {
    return zxvfs_write(fs, STATE_FILE, (const uint8_t *)st, sizeof(*st)) == 0
               ? AB_OK : AB_ERR_IO;
}

ab_result_t ab_init(zxvfs_t *fs, ab_state_t *st) {
    if (!fs || !st) return AB_ERR_STATE;
    int n = zxvfs_read(fs, STATE_FILE, (uint8_t *)st, sizeof(*st));
    /* The persisted state is on untrusted media. active_slot indexes
     * SLOT_FILE[2] and version[2]; an out-of-range value read back verbatim
     * is an OOB array access (red-team). Accept only a well-formed record;
     * anything else falls through to a clean fresh install. */
    if (n == (int)sizeof(*st) && st->magic == AB_STATE_MAGIC &&
        st->active_slot <= 1 &&
        (st->probation_slot <= 1 || st->probation_slot == AB_SLOT_NONE))
        return AB_OK;
    /* Fresh install: slot A active, nothing on probation. */
    for (uint32_t i = 0; i < sizeof(*st); i++) ((uint8_t *)st)[i] = 0;
    st->magic = AB_STATE_MAGIC;
    st->active_slot = 0;
    st->probation_slot = AB_SLOT_NONE;
    st->version[0] = 1;
    st->version[1] = 0;
    st->rollback_floor = 0;
    return save_state(fs, st);
}

ab_result_t ab_stage_update(zxvfs_t *fs, ab_state_t *st,
                            const uint8_t *zsp, uint32_t len,
                            const uint8_t root_pubkey[32]) {
    if (!fs || !st || !zsp) return AB_ERR_STATE;

    /* Verify BEFORE touching any slot — a bad update must not perturb
     * the running system at all. A ZSP v2 package is verified with the
     * monotonic anti-rollback FLOOR enforced (P0-6) and must then be STRICTLY
     * newer than the active slot: an old or equal validly-signed package is
     * refused with AB_ERR_ROLLBACK. Its authenticated version becomes the
     * slot's version.
     *
     * A legacy v1 package carries no signed version, so it can never prove it
     * is newer; any old validly-signed v1 image could be replayed. Once its
     * signature checks, it is refused with AB_ERR_ROLLBACK (the earlier code
     * gave it version active+1, which let an old v1 image replace a newer one,
     * and before the audit fix also replace an active v2 package). */
    const uint8_t *payload; uint32_t plen;
    uint32_t new_version;
    if (is_zsp2(zsp, len)) {
        zsp_meta_t meta;
        zsp_result_t r = zsp_verify2(zsp, len, root_pubkey, st->rollback_floor,
                                     ZSP_ARCH_ANY, &meta, &payload, &plen);
        if (r == ZSP_ERR_ROLLBACK) return AB_ERR_ROLLBACK;
        if (r != ZSP_OK)           return AB_ERR_VERIFY;
        if (!version_allowed(st, meta.version)) return AB_ERR_ROLLBACK;
        /* The A/B pair holds ONE application: when the active slot is a v2
         * package, the new one must carry the same signed package identity,
         * so another root-signed package cannot be swapped in as an "update". */
        uint8_t cur[ZSP2_HEADER_LEN];
        int cn = zxvfs_read(fs, SLOT_FILE[st->active_slot], cur, sizeof cur);
        if (cn == (int) sizeof cur && is_zsp2(cur, (uint32_t) cn)) {
            for (uint32_t i = 0; i < 32; i++)
                if (cur[32 + i] != meta.identity[i]) return AB_ERR_VERIFY;
        }
        new_version = meta.version;
    } else {
        if (zsp_verify(zsp, len, root_pubkey, &payload, &plen) != ZSP_OK)
            return AB_ERR_VERIFY;
        return AB_ERR_ROLLBACK; /* unversioned: monotonicity cannot be shown */
    }

    uint8_t target = st->active_slot ? 0 : 1;   /* the inactive slot */
    /* Durability ordering (P0-3): the payload is written to the INACTIVE slot
     * (never the live one) and its journaled write commits BEFORE the state
     * record is updated to point probation at it. A crash between the two leaves
     * state still referencing only the known-good active slot. */
    if (zxvfs_write(fs, SLOT_FILE[target], zsp, len) != 0)
        return AB_ERR_IO;

    st->probation_slot = target;
    st->version[target] = new_version;
    st->probation_remaining = AB_DEFAULT_PROBATION;
    return save_state(fs, st);
}

ab_result_t ab_slot_to_run(zxvfs_t *fs, ab_state_t *st,
                           uint8_t *buf, uint32_t max, uint32_t *out_len,
                           uint8_t *slot_out) {
    if (!fs || !st || !buf) return AB_ERR_STATE;
    uint8_t slot = (st->probation_slot != AB_SLOT_NONE)
                       ? st->probation_slot : st->active_slot;
    int n = zxvfs_read(fs, SLOT_FILE[slot], buf, max);
    if (n < 0) {
        /* Active slot missing is fatal; probation slot missing rolls back. */
        if (slot != st->active_slot) {
            st->probation_slot = AB_SLOT_NONE;
            st->rollbacks++;
            save_state(fs, st);
        }
        return AB_ERR_IO;
    }
    if (out_len) *out_len = (uint32_t)n;
    if (slot_out) *slot_out = slot;
    return AB_OK;
}

ab_result_t ab_confirm(zxvfs_t *fs, ab_state_t *st) {
    if (!fs || !st) return AB_ERR_STATE;
    if (st->probation_slot == AB_SLOT_NONE) return AB_ERR_NONE;
    /* Promotion re-checks monotonicity instead of trusting the state record
     * (which lives on the same untrusted media): the probation slot must hold
     * a ZSP v2 package whose header version is the one recorded at staging,
     * strictly above the active version and at or above the floor. Anything
     * else is a rollback attempt: the probation slot is discarded. */
    uint8_t hdr[ZSP2_HEADER_LEN];
    uint8_t ps = st->probation_slot;
    int hn = zxvfs_read(fs, SLOT_FILE[ps], hdr, sizeof hdr);
    if (hn != (int) sizeof hdr || !is_zsp2(hdr, (uint32_t) hn) ||
        rd32le(hdr + 8) != st->version[ps] || !version_allowed(st, st->version[ps])) {
        st->probation_slot = AB_SLOT_NONE;
        st->probation_remaining = 0;
        st->rollbacks++;
        (void) save_state(fs, st);
        return AB_ERR_ROLLBACK;
    }
    st->active_slot = st->probation_slot;
    st->probation_slot = AB_SLOT_NONE;
    st->probation_remaining = 0;
    st->promotions++;
    /* P0-6: once a version is confirmed good, raise the monotonic floor to it so
     * no older (even validly-signed) package can ever be staged again. */
    if (st->version[st->active_slot] > st->rollback_floor)
        st->rollback_floor = st->version[st->active_slot];
    return save_state(fs, st);
}

ab_result_t ab_rollback(zxvfs_t *fs, ab_state_t *st) {
    if (!fs || !st) return AB_ERR_STATE;
    if (st->probation_slot == AB_SLOT_NONE) return AB_ERR_NONE;
    st->probation_slot = AB_SLOT_NONE;
    st->probation_remaining = 0;
    st->rollbacks++;
    return save_state(fs, st);
}

ab_result_t ab_boot_tick(zxvfs_t *fs, ab_state_t *st, bool *did_rollback) {
    if (did_rollback) *did_rollback = false;
    if (!fs || !st) return AB_ERR_STATE;
    if (st->probation_slot == AB_SLOT_NONE) return AB_OK;

    if (st->probation_remaining > 0)
        st->probation_remaining--;

    if (st->probation_remaining == 0) {
        /* Probation elapsed with no confirm -> auto-rollback. */
        st->probation_slot = AB_SLOT_NONE;
        st->rollbacks++;
        if (did_rollback) *did_rollback = true;
    }
    return save_state(fs, st);
}
