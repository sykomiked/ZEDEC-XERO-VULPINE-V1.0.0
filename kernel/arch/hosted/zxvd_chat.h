/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxvd_chat.h — the owner's social chat in zxvd: signed posts, replies,
 * reactions and follows (social_post.h), kept in a local store
 * (social_store.h) and ranked by the ISF feed (social_feed.h). The methods
 * reach the app through zxvd's extension table (zxvd_rpc.h X1-X4).
 *
 *   C1 IDENTITY. The owner's chat key is ML-DSA-65 from a 32-byte seed in
 *      "<prefix>.seed" (created 0600 from the OS random source on first
 *      use, exactly 32 bytes after that). NodeID = SHA-256(public key)
 *      (social_post.h P1). This key is separate from the mesh node's
 *      ML-DSA-87 identity (vna_id.h): one person can keep one chat identity
 *      across mesh nodes.
 *   C2 LOG. Every record the owner makes is appended to "<prefix>.log"
 *      and fsync'd before the method answers: the 8-byte header "ZXSLOG1\n",
 *      then frames of a 4-byte big-endian length and the record's
 *      social_post.h encoding, public key attached. At open each frame is
 *      decoded and its signature checked (sp_decode); a frame cut short at
 *      the end (a crash mid-write) is cut off the file, and any other bad
 *      frame stops the start, so a damaged log is never quietly dropped.
 *      A record that cannot be saved is not kept, and the method fails.
 *   C3 MEMORY ONLY. With no prefix (zxvd --books -) the key is fresh for
 *      this run and nothing is saved; chat.me says "saved":false.
 *   C4 STORE. The newest ZXVD_CHAT_CAP records are held for the feed
 *      (social_store.h S1); older ones stay in the log and come back at the
 *      next start only if they are among the newest.
 *   C5 METHODS.
 *        chat.me      {} -> {"node_id","saved","records","following":[id]}
 *        chat.post    {"text", "reply_to"?} -> a record (below)
 *        chat.react   {"to", "emoji"} -> a record
 *        chat.follow  {"node_id", "follow"?=true} -> {"node_id","following"}
 *        chat.feed    {} -> {"posts":[record]} best first, at most
 *                     ZXVD_CHAT_FEED, ranked by social_feed.h F2-F5 with no
 *                     seen set (every post starts with full surplus) and
 *                     the default freshness term
 *        chat.thread  {"id"} -> {"root":record,"replies":[record]} with the
 *                     replies in (Lamport, id) order
 *      A record is {"id","author","mine","kind":"post"|"reply"|"reaction",
 *      "lamport","wall_ms","text","reply_to":id|null,"replies",
 *      "reactions":[{"emoji","count","mine"}]}. Ids and NodeIDs are 64
 *      lower-case hex digits. lamport and wall_ms are decimal strings.
 *   C6 TEXT. A post or reply is 1..SP_BODY_MAX bytes of UTF-8 with at least
 *      one byte that is not a space, tab or line break; a reaction is
 *      1..SP_REACT_MAX bytes of UTF-8. Text from a record that is not valid
 *      UTF-8 (a peer's) is shown with U+FFFD for each bad byte.
 *   C7 FOLLOWING. The newest FOLLOW record per followee (Lamport, then id)
 *      decides; a repeat of the current state writes nothing.
 *   C8 CLOCK. The Lamport clock observes every record held, so a new record
 *      is always later than everything the owner has seen.
 *
 * HONEST LIMITS: records stay on this device. There is no exchange with
 * peers yet (social_store.h gossip is built, its transport over the mesh is
 * not), and no direct messages (they need the session encryption of
 * social_post.h P9). The feed keeps no seen set between calls. Nothing is
 * ever deleted (social_post.h); there is no edit, only a new record.
 */
#ifndef ZXVD_CHAT_H
#define ZXVD_CHAT_H

#include <stdbool.h>
#include <stdint.h>

#define ZXVD_CHAT_CAP  512u
#define ZXVD_CHAT_FEED 50u

/* C1-C3: open the chat with files "<prefix>.seed" and "<prefix>.log", or in
 * memory if prefix is NULL or "". Registers the chat.* methods with zxvd_rpc.
 * 0, or -1 with the reason on stderr (nothing registered). */
int zxvd_chat_open(const char *prefix);
void zxvd_chat_close(void);

/* The wall clock in ms for new records (a hint only, social_post.h P2);
 * NULL leaves wall_ms out. Tests set a fixed one. */
void zxvd_chat_set_clock(uint64_t (*now_ms)(void));

/* R24 receipts (zxv_receipt.h E3): sign a 32-byte digest with the owner's
 * chat key (ML-DSA-65, the key chat records are signed with). sig gets
 * ZXVD_CHAT_SIG_LEN bytes; pk_id the SHA-256 of the public key; pk (may be
 * NULL) the public key itself. 0, or -1 when the chat is not open or signing
 * failed. */
#define ZXVD_CHAT_SIG_LEN 3309u
#define ZXVD_CHAT_PK_LEN  1952u
int zxvd_chat_sign_digest(const uint8_t digest[32], uint8_t sig[ZXVD_CHAT_SIG_LEN],
                          uint8_t pk_id[32], uint8_t *pk);

#endif /* ZXVD_CHAT_H */
