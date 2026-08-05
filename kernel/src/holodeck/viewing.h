/* viewing.h — a Holodeck viewing held INSIDE a den
 *
 * WHY THIS COMPOSITION MATTERS
 * ----------------------------
 * A swarm on its own is an open firehose: anyone who learns a content root can
 * join and pull. That is right for a public commons and wrong for a family
 * den. Putting the viewing inside a den means the den's EXISTING rules govern
 * it — no second permission system to write, and no second one to get wrong:
 *
 *   * den_may_connect decides who can be in the room at all, so a PRIVATE den's
 *     viewing is invisible to strangers and a SELECT den's is invite-only.
 *   * DEN_DOWNLOAD decides who may RECEIVE chunks; DEN_UPLOAD decides who may
 *     SERVE them. A guest can watch without being conscripted as a seeder.
 *   * A banned identity is refused, because den_may_connect already refuses it.
 *   * DEN_SEND_CHAT governs the social layer around the film, exactly as it
 *     governs the den's ordinary chat.
 *
 * The swarm keeps its own honest arithmetic: capacity still rises with every
 * viewer who is PERMITTED to serve, so a den full of contributors is fast and
 * a den of pure spectators falls back on its seeds. Access control changes who
 * may participate; it does not change the physics.
 *
 * ACCOUNTABILITY COMES ALONG FOR FREE
 * -----------------------------------
 * Because the den already hash-chains every rule change, an operator who
 * quietly revokes someone's access mid-viewing leaves a tamper-evident trace.
 * Sovereignty over your own den, with a record.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Holodeck/Den composition)
 * License: SEL-3.3
 */
#ifndef ZXV_VIEWING_H
#define ZXV_VIEWING_H

#include "swarm.h"
#include "../denconnect/denconnect.h"

/* A viewing = one swarm room governed by one den. */
typedef struct {
    den_server_t *den;      /* the governing den (not owned) */
    swarm_room_t  room;
} viewing_t;

/* Open a viewing inside `den`. Returns false on bad arguments. */
bool viewing_open(viewing_t *v, den_server_t *den,
                  const uint8_t content_root[SWARM_DIGEST_LEN],
                  uint32_t n_chunks, uint32_t origin_kbps);

/* May this identity be in the room at all? (the den's visibility tier) */
bool viewing_may_watch(const viewing_t *v, const uint8_t key[DEN_KEY_LEN]);

/* May this identity SERVE chunks to others? (DEN_UPLOAD) */
bool viewing_may_seed(const viewing_t *v, const uint8_t key[DEN_KEY_LEN]);

/* May this identity talk in the room? (DEN_SEND_CHAT) */
bool viewing_may_speak(const viewing_t *v, const uint8_t key[DEN_KEY_LEN]);

/* Join a viewing. The identity must be allowed to connect AND hold
 * DEN_DOWNLOAD. `upload_kbps` is only credited to the swarm if the identity
 * also holds DEN_UPLOAD — a viewer who may not seed contributes 0, honestly.
 * Returns the swarm slot, or -1 if refused. */
int32_t viewing_join(viewing_t *v, const uint8_t key[DEN_KEY_LEN],
                     uint32_t peer_id, uint32_t upload_kbps);

/* Aggregate capacity of the viewing (delegates to the swarm). */
uint32_t viewing_capacity_kbps(const viewing_t *v);

#endif /* ZXV_VIEWING_H */
