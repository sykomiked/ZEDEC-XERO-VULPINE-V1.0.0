/* abupdate.c — A/B signed-package update with probation + auto-rollback.
 * See abupdate.h. Every mutation is a single journaled ZXVFS write, so a
 * crash leaves the system in a consistent A-or-B state.
 */
#include "abupdate.h"
#include "zsp.h"

static const char *SLOT_FILE[2] = { "app.slotA", "app.slotB" };
static const char *STATE_FILE = "app.state";

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
    return save_state(fs, st);
}

ab_result_t ab_stage_update(zxvfs_t *fs, ab_state_t *st,
                            const uint8_t *zsp, uint32_t len,
                            const uint8_t root_pubkey[32]) {
    if (!fs || !st || !zsp) return AB_ERR_STATE;

    /* Verify BEFORE touching any slot — a bad update must not perturb
     * the running system at all. */
    const uint8_t *payload; uint32_t plen;
    if (zsp_verify(zsp, len, root_pubkey, &payload, &plen) != ZSP_OK)
        return AB_ERR_VERIFY;

    uint8_t target = st->active_slot ? 0 : 1;   /* the inactive slot */
    if (zxvfs_write(fs, SLOT_FILE[target], zsp, len) != 0)
        return AB_ERR_IO;

    st->probation_slot = target;
    st->version[target] = st->version[st->active_slot] + 1;
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
    st->active_slot = st->probation_slot;
    st->probation_slot = AB_SLOT_NONE;
    st->probation_remaining = 0;
    st->promotions++;
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
