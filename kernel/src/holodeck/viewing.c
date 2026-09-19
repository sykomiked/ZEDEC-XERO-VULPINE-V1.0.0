/* viewing.c — a Holodeck viewing held inside a den. See viewing.h.
 * Pure composition: the den answers every permission question, the swarm does
 * every byte of delivery. No new access-control logic exists here. */
#include "viewing.h"

bool viewing_open(viewing_t *v, den_server_t *den,
                  const uint8_t content_root[SWARM_DIGEST_LEN],
                  uint32_t n_chunks, uint32_t origin_kbps) {
    if (!v || !den) return false;
    v->den = den;
    swarm_init(&v->room, content_root, n_chunks, origin_kbps);
    return true;
}

bool viewing_may_watch(const viewing_t *v, const uint8_t key[DEN_KEY_LEN]) {
    if (!v || !v->den || !key) return false;
    /* the den's visibility tier and ban list already answer this */
    return den_may_connect(v->den, key) && den_can(v->den, key, DEN_DOWNLOAD);
}

bool viewing_may_seed(const viewing_t *v, const uint8_t key[DEN_KEY_LEN]) {
    if (!v || !v->den || !key) return false;
    return den_may_connect(v->den, key) && den_can(v->den, key, DEN_UPLOAD);
}

bool viewing_may_speak(const viewing_t *v, const uint8_t key[DEN_KEY_LEN]) {
    if (!v || !v->den || !key) return false;
    return den_may_connect(v->den, key) && den_can(v->den, key, DEN_SEND_CHAT);
}

int32_t viewing_join(viewing_t *v, const uint8_t key[DEN_KEY_LEN],
                     uint32_t peer_id, uint32_t upload_kbps) {
    if (!viewing_may_watch(v, key)) return -1;
    /* Credit upload ONLY if this identity is permitted to serve. A spectator
     * is not silently conscripted as a seeder, and the swarm's capacity
     * arithmetic stays honest about who is actually contributing. */
    uint32_t credited = viewing_may_seed(v, key) ? upload_kbps : 0u;
    return swarm_join(&v->room, peer_id, credited);
}

uint32_t viewing_capacity_kbps(const viewing_t *v) {
    return v ? swarm_capacity_kbps(&v->room) : 0u;
}

/* ---- DECLARATION -----------------------------------------------------------

 * ROOTING THE CALLER, NOT THE LEAF. viewing.o was measured at linked=0,
 * dropped=6. The fix is not to root den_can or swarm_init -- those are leaves,
 * and rooting them would make `nm` report a permission system and a swarm that
 * nothing consults. viewing IS the caller: viewing_may_watch calls
 * den_may_connect and den_can, viewing_capacity_kbps calls
 * swarm_capacity_kbps. Rooting here pulls denconnect and swarm in behind it.
 *
 * REQUIRES measured from viewing.o's `nm -u` = {den_can, den_may_connect,
 * swarm_capacity_kbps, swarm_init, swarm_join}.
 *
 * The bring-up runs the DENIAL path on a viewing with no den. That is not a
 * degenerate case chosen for safety -- viewing_may_watch's first line is
 * `if (!v || !v->den || !key) return false`, and a permission check that
 * answers "yes" when it has no policy server is the worst bug this file could
 * have. Checking that it says no is checking the thing that matters.
 */
#include "zxv_decl.h"
static int zxvd_viewing_bringup(void) {
    static viewing_t v;                  /* zeroed: no den, empty room */
    static const uint8_t key[DEN_KEY_LEN];
    if (viewing_may_watch(&v, key)) return -1;   /* no den => no permission */
    if (viewing_may_seed(&v, key))  return -1;
    if (viewing_may_speak(&v, key)) return -1;
    if (viewing_capacity_kbps(&v) != 0u) return -1;
    return 0;
}

ZXV_DECLARE(viewing,
    ZXV_PROVIDES(viewing_policy_ready),
    ZXV_REQUIRES(denconnect_ready, swarm_ready),
    ZXV_BRINGUP(zxvd_viewing_bringup));
