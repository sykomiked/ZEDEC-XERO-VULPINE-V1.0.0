/* swarm.c — the Holodeck swarm. See swarm.h. */
#include "swarm.h"
#include "../robin_debanks/sha256.h"

void swarm_init(swarm_room_t *r, const uint8_t content_root[SWARM_DIGEST_LEN],
                uint32_t n_chunks, uint32_t origin_kbps) {
    if (!r) return;
    for (uint32_t i = 0; i < SWARM_DIGEST_LEN; i++)
        r->content_root[i] = content_root ? content_root[i] : 0;
    r->n_chunks = (n_chunks <= SWARM_MAX_CHUNKS) ? n_chunks : SWARM_MAX_CHUNKS;
    for (uint32_t i = 0; i < SWARM_MAX_CHUNKS; i++) {
        r->chunk[i].length = 0;
        r->chunk[i].present = false;
        for (uint32_t d = 0; d < SWARM_DIGEST_LEN; d++) r->chunk[i].digest[d] = 0;
    }
    for (uint32_t i = 0; i < SWARM_MAX_PEERS; i++) {
        r->peer[i].active = false;
        r->peer[i].peer_id = 0;
        r->peer[i].upload_kbps = 0;
        r->peer[i].chunks_served = 0;
        for (uint32_t c = 0; c < SWARM_MAX_CHUNKS; c++) r->peer[i].has_chunk[c] = 0;
    }
    r->n_peers = 0;
    r->origin_kbps = origin_kbps;
    r->play_ordinal = 0;
    r->play_chunk = 0;
}

bool swarm_set_chunk(swarm_room_t *r, uint32_t idx,
                     const uint8_t digest[SWARM_DIGEST_LEN], uint32_t length) {
    if (!r || !digest || idx >= r->n_chunks) return false;
    for (uint32_t i = 0; i < SWARM_DIGEST_LEN; i++) r->chunk[idx].digest[i] = digest[i];
    r->chunk[idx].length = length;
    return true;
}

static swarm_peer_t *find_peer(swarm_room_t *r, uint32_t peer_id) {
    for (uint32_t i = 0; i < SWARM_MAX_PEERS; i++)
        if (r->peer[i].active && r->peer[i].peer_id == peer_id) return &r->peer[i];
    return 0;
}

int32_t swarm_join(swarm_room_t *r, uint32_t peer_id, uint32_t upload_kbps) {
    if (!r || peer_id == 0) return -1;
    if (find_peer(r, peer_id)) return -1;              /* already here */
    for (uint32_t i = 0; i < SWARM_MAX_PEERS; i++) {
        if (r->peer[i].active) continue;
        r->peer[i].active = true;
        r->peer[i].peer_id = peer_id;
        r->peer[i].upload_kbps = upload_kbps;          /* brings its own upload */
        r->peer[i].chunks_served = 0;
        for (uint32_t c = 0; c < SWARM_MAX_CHUNKS; c++) r->peer[i].has_chunk[c] = 0;
        r->n_peers++;
        return (int32_t)i;
    }
    return -1;
}

bool swarm_leave(swarm_room_t *r, uint32_t peer_id) {
    swarm_peer_t *p = r ? find_peer(r, peer_id) : 0;
    if (!p) return false;
    p->active = false;
    if (r->n_peers) r->n_peers--;
    return true;
}

bool swarm_peer_has(swarm_room_t *r, uint32_t peer_id, uint32_t chunk_idx) {
    if (!r || chunk_idx >= r->n_chunks) return false;
    swarm_peer_t *p = find_peer(r, peer_id);
    if (!p) return false;
    p->has_chunk[chunk_idx] = 1;
    return true;
}

/* The headline property: every viewer adds their own upload, so aggregate
 * capacity RISES with the audience instead of being consumed by it. */
uint32_t swarm_capacity_kbps(const swarm_room_t *r) {
    if (!r) return 0;
    uint32_t total = r->origin_kbps;
    for (uint32_t i = 0; i < SWARM_MAX_PEERS; i++)
        if (r->peer[i].active) total += r->peer[i].upload_kbps;
    return total;
}

uint32_t swarm_chunk_sources(const swarm_room_t *r, uint32_t chunk_idx) {
    if (!r || chunk_idx >= r->n_chunks) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < SWARM_MAX_PEERS; i++)
        if (r->peer[i].active && r->peer[i].has_chunk[chunk_idx]) n++;
    return n;
}

uint32_t swarm_select_source(const swarm_room_t *r, uint32_t chunk_idx,
                             uint32_t requester_id) {
    if (!r || chunk_idx >= r->n_chunks) return 0;
    /* Prefer the willing holder that has served LEAST: load spreads across
     * the swarm instead of concentrating on whoever joined first. */
    uint32_t best_id = 0, best_served = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < SWARM_MAX_PEERS; i++) {
        const swarm_peer_t *p = &r->peer[i];
        if (!p->active || p->peer_id == requester_id) continue;
        if (!p->has_chunk[chunk_idx]) continue;
        if (p->upload_kbps == 0) continue;             /* cannot serve */
        if (p->chunks_served < best_served) { best_served = p->chunks_served; best_id = p->peer_id; }
    }
    return best_id;
}

bool swarm_verify_chunk(const swarm_room_t *r, uint32_t chunk_idx,
                        const uint8_t *data, uint32_t len) {
    if (!r || !data || chunk_idx >= r->n_chunks) return false;
    uint8_t got[SWARM_DIGEST_LEN];
    sha256(data, len, got);
    for (uint32_t i = 0; i < SWARM_DIGEST_LEN; i++)
        if (got[i] != r->chunk[chunk_idx].digest[i]) return false;
    return true;
}

void swarm_advance(swarm_room_t *r, uint64_t ordinal) {
    if (!r) return;
    if (ordinal <= r->play_ordinal) return;            /* never runs backwards */
    r->play_ordinal = ordinal;
    if (r->n_chunks) r->play_chunk = (uint32_t)(ordinal % r->n_chunks);
}

uint32_t swarm_position(const swarm_room_t *r) { return r ? r->play_chunk : 0; }

/* ---- companions ---- */
bool swarm_companions_may_share(const swarm_companion_t *a,
                                const swarm_companion_t *b) {
    if (!a || !b) return false;
    if (a->peer_id == b->peer_id) return false;
    return a->sharing_enabled && b->sharing_enabled;   /* BOTH owners opted in */
}

surplus_real_t swarm_companion_gain(const swarm_companion_t *self,
                                    const swarm_companion_t *others,
                                    uint32_t n_others,
                                    uint32_t *distinct_out) {
    if (distinct_out) *distinct_out = 0;
    if (!self || !self->sharing_enabled) return SR_ZERO;  /* opted out: no gain */

    surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
    uint32_t n = 0;
    for (uint32_t d = 0; d < CHG_DIM; d++) ev[n][d] = self->view[d];
    n++;
    for (uint32_t k = 0; k < n_others && n < CHG_MAX_EXPERTS; k++) {
        if (!swarm_companions_may_share(self, &others[k])) continue;
        for (uint32_t d = 0; d < CHG_DIM; d++) ev[n][d] = others[k].view[d];
        n++;
    }
    /* The same ISF rule as everywhere else: duplicated perspective collapses,
     * so a companion grows from DIFFERENCE, never from crowd size. */
    return chg_effective_experts((const surplus_real_t (*)[CHG_DIM])ev,
                                 n, CHG_DIM, distinct_out);
}
