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
