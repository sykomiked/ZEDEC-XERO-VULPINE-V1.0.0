# Endian Hopping (`kernel/src/ehop`)

Endian Hopping is a data layer built on alternating byte order. It splits each
frame into words, flips the byte order of some groups of words and leaves
others alone, and changes which groups flip according to a secret schedule.
The rate of flipping works like a frequency. Different rates, phases and flip
depths give different channels on the same link, and groups of people can
form private networks that outsiders can neither read nor route.

It always runs inside the existing authenticated encryption and never
replaces it. The security statement is in the next section, and the rest of
this document should be read in its light.

## 1. The honest security statement

- **Confidentiality and integrity come from the AEAD and nothing else.** The
  AEAD is ChaCha20-Poly1305 (RFC 8439, `kernel/src/tls/aead.c`), keyed per
  channel and epoch.
- Alternating endianness on its own is a public, fixed permutation of bytes.
  Anyone can undo it, so on its own it is obfuscation, not encryption. Keying
  the schedule with SHAKE256 makes it unpredictable to outsiders. It is still
  only a permutation of the plaintext's bytes and does not count as a cipher.
  No claim in this document depends on the permutation being hard to undo.
- What the keyed alternation does add:
  1. **Channel separation.** Every key a channel uses (schedule key, AEAD key
     and tag key) is derived from the full channel definition: secret,
     channel id, epoch, word width, band, AM depth set, FM rate, FM hop, PM
     mode, PM phase and PM step. A receiver with any different parameter
     derives a different AEAD key, and the Poly1305 check fails. The tests
     confirm this for a different key, FM rate, AM depth and PM phase, even
     when the attacker forges a tag that passes routing.
  2. **Traffic-shape diversity.** Two channels carrying the same plaintext
     produce unrelated byte orders before encryption. This is useful as
     defence in depth, for example if a future AEAD misuse leaks plaintext
     structure.
  3. **Routing without decryption.** A keyed tag lets a node file a frame
     under the right channel by computing one hash per candidate channel.
- **Cost, measured on this machine** (Intel Xeon at 2.80 GHz, one core,
  `gcc -O2`, host build; the figures vary by about ±25 % from run to run):

  | Operation | Throughput |
  |---|---|
  | Transform alone, worst case (16-bit words, a flip every word) | 600–810 MB/s |
  | Transform alone, settlement-style channel (64-bit words, rate 16, hop 8) | 3.0–4.3 GB/s |
  | Transform alone, bulk channel (64-bit words, rate 1024) | 3.8–5.2 GB/s |
  | Bare ChaCha20-Poly1305 seal, 2048-byte frames | 230–265 MB/s |
  | Full `ehop_seal` (schedule + tag + AEAD), 2048-byte frames | 128–147 MB/s |
  | Seal followed by open, 2048-byte frames | 63–76 MB/s |

  The byte reordering itself is cheap: even the worst case runs about 3 times
  faster than the AEAD, and typical channels run 12 to 20 times faster. Most
  of the extra cost per frame comes from the fixed per-frame hashing: one
  SHAKE256 call to derive the schedule (about 2.3 µs here) and one SHA3-256
  call for the channel tag. The Keccak in this tree is a plain portable
  implementation. As a result, a full seal runs at about 55 % of the speed of
  a bare AEAD seal for 2048-byte frames. That is the honest overhead. It is
  cheap, but it is not free.
- **What it does not do.** It does not hide frame sizes, timing or who talks
  to whom. The band byte in the clear header reveals the traffic class (see
  §4). It gives no protection against an endpoint that is already
  compromised. It does not replace key management.

## 2. How the rate becomes a frequency

A frame's plaintext is read as a sequence of *words* of 16, 32 or 64 bits.
The words are grouped into *segments*, and the segments alternate:

```
words:   | w0 w1 w2 | w3 w4 w5 w6 | w7 w8 | w9 w10 w11 w12 w13 | ...
segment:   on (flip)    off          on       off
```

The schedule borrows the vocabulary of radio modulation. **This is an
analogy for the parameters of a byte-order schedule. Nothing here is a radio
waveform, and no signal is transmitted differently.**

| Radio | Endian Hopping | Parameter | Effect |
|---|---|---|---|
| **FM**, frequency | how often the byte order flips | `fm_rate`: mean words per segment | A rate of 16 means a flip every 16 words on average. With 64-bit words that is one flip per 1024 bits, so a 1 Gbit/s link carries about a million flips per second. This is the sense in which the rate is a frequency. |
| FM hopping (spread spectrum) | the length of each segment varies | `fm_hop`: each segment is `rate − hop … rate + hop` words, chosen by the keyed stream | The tests measure a mean of 15.82 words per flip for rate 16, with every segment inside 10…22. |
| **PM**, phase | where in the pattern the first flip lands | `pm_phase` (base), `pm_mode` static / per-epoch / per-frame, `pm_step` | phase = base + step × (0, epoch or sequence number) + a keyed per-frame offset, all mod rate. The first segment is `rate − phase` words long. |
| **AM**, amplitude or depth | how much of an "on" word is reordered | `am_mask`: the allowed set of depths | **Nibble**: swap the two halves of every byte (lightest). **Pair**: swap adjacent bytes. **Half**: swap the two halves of the word. **Full**: reverse every byte (the classic endianness flip). When more than one depth is allowed, each "on" segment draws its depth from the keyed stream. |

Each depth is its own inverse, so applying the same schedule twice gives back
the original bytes (`ehop_apply` is involutive). On a 16-bit word, pair, half
and full all mean "swap the two bytes". A trailing partial word is reordered
as a short word and remains an involution.

**Schedule derivation** (all of it lives in `ehop_sched.c`):

```
seed[32] || r[4] = SHAKE256("ZXV-EHOP-v1/sched" || channel schedule key ||
                            chan_id || epoch || sender || seq || cfg)
start_on  = r[0] & 1
phase     = (pm_phase + pm_step·X + (le16(r[1..2])·rate >> 16)) mod rate
hop stream = ChaCha20 keystream under key = seed (nonce 0)
```

The hop stream expands the SHAKE256 seed with ChaCha20 because the tree's
ChaCha20 produces bytes several times faster than its Keccak. This matters
because a low FM rate reads a lot of the stream. The seed is unique per frame
because sequence numbers are never reused, so no ChaCha20 key and nonce pair
repeats.

Every node holding the same inputs derives the same schedule. The tests check
this, and they also check that changing any single input (key, channel id,
epoch, sender, sequence number, rate, hop, phase, depth or width) changes the
output.

A very low frequency (a rate in the bulk band, such as 1024 words) can leave
a whole short frame in a single "off" segment, in which case nothing is
reordered. That is expected: the AEAD still protects the frame.

## 3. Channels

A **channel** is the tuple *(secret, AM, FM, PM, hop sequence)*. Concretely,
it is a 32-byte secret, a channel id, an epoch and an `ehop_cfg_t`. From these
`ehop_channel_init` derives three keys:

```
sched_key || aead_key || tag_key = SHAKE256("ZXV-EHOP-v1/chan" || secret ||
                                            chan_id || epoch || cfg, 96)
```

The secret can be a pairwise ML-KEM-768 shared secret, or a key derived from
a private network (§5).

**Order of operations.** On sending, the schedule is applied to the
*plaintext* and the AEAD encrypts the result. On receiving, the AEAD is
verified and decrypted first, and only then is the schedule undone. Applying
the schedule to the ciphertext instead would only permute bytes that are
already uniformly random, which an observer cannot tell apart from no change.
Inside the AEAD is therefore the only place where the transform has any
effect.

**Frame layout** (48 bytes of overhead):

```
hdr[24]  "ZH" | version | band | le32 epoch | le32 sender | le64 seq | le16 len | le16 0
tag[8]   channel tag = SHA3-256(tag_key || "ZXV-EHOP-v1/tag" || hdr)[0..8]
ct[len]  ChaCha20(aead_key, nonce = le32 sender || le64 seq, schedule(pt))
mac[16]  Poly1305 over (hdr || tag) as AAD and ct
```

**Channel tag.** A node holding several channels on a link computes one
truncated SHA3 per candidate channel and compares the results in constant
time (`ehop_route`), so it never has to attempt a decryption. The tag covers
the sequence number, so it changes with every frame. An observer therefore
does not see a fixed channel label it could follow, and without `tag_key`
nobody can compute it. The tag only routes frames. Any frame that passes the
tag check must still pass the AEAD check.

**Replay.** Each channel tracks up to 16 senders, with a 64-frame sliding
window per sender. A replayed frame is rejected. A frame that arrives late but
still inside the window is accepted once. A 17th sender on one channel is
refused (`EHOP_EFULL`), so it is never accepted without the replay check.

**Nonce discipline.** The AEAD nonce is the pair (sender, seq). Every node
that seals on a channel needs a unique sender id; in a private network this
is its member id. A node that re-derives the same channel within the same
epoch (for example after a reboot) must restore its saved counter with
`ehop_channel_set_seq`, which refuses to move backwards. A node must also
seal on only one copy of a channel.

## 4. Bands: the band plan and QoS

A **band** is a named region of the (FM, PM, AM) space reserved for one
traffic class. The bands occupy disjoint FM ranges, so the band a channel
belongs to follows from its configuration. `ehop_cfg_check` refuses any
configuration that falls outside its band.

| Band | Traffic classes | FM rate (words per flip) | AM depths allowed | PM modes | Priority | Max frame | Latency target |
|---|---|---|---|---|---|---|---|
| `control` | control, routing | 1–7 | full, half | all | 0 (first) | 560 B | 10 ms |
| `settle` | payment, settlement | 8–31 | full, half, pair | all | 1 | 1072 B | 50 ms |
| `media` | voice, video | 32–127 | nibble, pair | static, per-frame | 2 | 1400 B | 30 ms |
| `chat` | chat, alert | 128–511 | all | all | 3 | 2096 B | 250 ms |
| `bulk` | file | 512–4096 | all | all | 4 (last) | 2096 B | 2000 ms |

- `ehop_band_for_class(msg_class)` picks the band for a message class.
- The band number is carried in the clear header and is authenticated as part
  of the AAD, so it cannot be altered without detection.
  `ehop_frame_qos(frame)` returns the band and its priority **without any
  key**, so a forwarding node can serve settlement traffic before bulk
  traffic without decrypting anything. The cost is that observers can see the
  traffic class. A private network that wants to hide its traffic classes can
  run all its traffic in a single band.
- The latency targets and the size limits are hints for schedulers. Only the
  maximum frame size is enforced, by `ehop_seal`.

## 5. Private networks

A group can form its own network:

1. **Create.** The admin calls `ehop_net_create(net_id, admin_id,
   sig_seed, key_entropy)`. This makes an ML-DSA-65 key pair (FIPS 204, from
   `kernel/src/pqsec`) and the epoch-0 network key. All randomness comes from
   the caller, since the kernel module contains no random number generator.
2. **Invite.** Each member has an ML-KEM-768 key pair (FIPS 203,
   `kernel/src/mlkem`). The admin calls `ehop_net_invite(member_id, ek, …)`,
   which writes a 4489-byte **welcome** message containing:
   - an ML-KEM ciphertext addressed to the member;
   - the network key, wrapped with a pad derived by SHAKE256 from the
     encapsulated secret;
   - a SHA3 commitment to the key;
   - an ML-DSA-65 signature over all of the above.
3. **Join.** The member first sets up with `ehop_net_member_init(net_id,
   self_id, admin_pk)`, where the admin's public key is obtained out of band.
   It then calls `ehop_net_join(dk, welcome)`, which:
   - checks the signature;
   - checks that the welcome is addressed to this member of this network;
   - decapsulates and unwraps the key;
   - installs the key only if the commitment matches.

   Welcomes signed by an impostor, altered, meant for someone else, or
   decapsulated with the wrong key are all refused. Once joined, a member
   accepts only welcomes for newer epochs, so an old welcome cannot roll it
   back.
4. **Channels.** `ehop_net_channel(net, chan_id, cfg, member_id)` derives a
   channel from `SHAKE256("ZXV-EHOP-v1/netchan" || net_id || network key)`.
   Every member derives the same channels. A non-member cannot compute the
   channel tags, so its routing finds nothing (*unroutable*). Even with a
   forged tag that passes routing, its AEAD check fails (*unreadable*).
5. **Rotate (forward secrecy).** At each epoch boundary, every member calls
   `ehop_net_rotate`:
   `key(e+1) = SHAKE256("ZXV-EHOP-v1/ratchet" || net_id || e+1 || key(e))`.
   The old key is overwritten, and the hash runs one way only, so a key
   stolen later cannot decrypt earlier epochs. Frames from the previous epoch
   are refused once a member has rotated (`EHOP_EEPOCH`). Members must
   therefore agree on when epochs change; the epoch number is in every
   header.
6. **Remove (re-key).** The admin calls `ehop_net_remove(member_id, entropy,
   …)`. This deletes the member from the roster and derives the next epoch's
   key from **fresh entropy** mixed with the old key. It then writes one
   signed re-key welcome for each remaining member. The removed member never
   sees the entropy, and the tests confirm the outcome:
   - it cannot accept any of the re-key welcomes;
   - ratcheting its old key forward does not produce the new key;
   - it can neither route nor read the new epoch's frames.

   A rotation on its own gives forward secrecy. A removal gives
   post-compromise recovery for everyone who remains.

Limits: up to 8 members per network roster. A member removed and later
invited again must reset its state with `ehop_net_member_init` before
rejoining, because the no-rollback rule otherwise blocks a welcome for an
epoch it has already reached on its own.

## 6. Routing in the peer-to-peer overlay (no servers)

`ehop_router_t` holds a table for each neighbour: up to 16 peers, each with up
to 8 channels. Nothing in it depends on a central server. A server that joins
the network is simply a peer with a larger `capacity` hint.

- `ehop_router_add_peer(id, capacity)` adds a peer and
  `ehop_router_add_channel(peer, ch)` gives it a channel.
- `ehop_router_pick(peer, msg_class, len, &band)` chooses a channel in the
  message class's band if the frame fits. Otherwise it falls back to the
  nearest band of **lower** priority that has a channel and fits. Traffic is
  never promoted above its class, so bulk transfers cannot take over the
  settlement band.
- `ehop_router_send` and `ehop_router_recv` pick a channel and seal, or route
  by band and tag and then open.

### Hook for `kernel/src/vinea` (formerly carracho) and other transports

A transport does not need to know about schedules or keys. It holds an
`ehop_hook_t`:

```c
ehop_router_t router;      /* per-node, filled with peers and channels  */
ehop_hook_t hook;
ehop_router_hook(&router, &hook);

/* send: */  hook.seal(hook.ctx, peer, EHOP_MSG_SETTLEMENT, pt, n, frame, cap, &flen);
/* recv: */  hook.open(hook.ctx, peer, frame, flen, pt, cap, &n, &chan);
/* relay: */ hook.qos(frame, flen, &band, &priority);   /* no keys needed */
```

The integration plan for Vinea, which is not done yet:

- Vinea's per-frame transform hook (`vna_xform_t` in `vinea/vna_frame.h`)
  calls `hook.seal` and `hook.open`, mapping the peer's transport address to
  the router's peer index;
- Vinea frames reach 13,700 bytes (`VNA_MSG_WIRE_MAX`) but an ehop frame
  carries at most 2,048 (`EHOP_MAX_PAYLOAD`), and Vinea does not fragment, so
  the adapter must fragment or ehop must raise its limit;
- relays order frames by `hook.qos` priority.

No code in Vinea calls ehop today.

## 7. Configuration reference

```c
ehop_cfg_t settle = {
    .width = 8,                                   /* 64-bit words            */
    .band = EHOP_BAND_SETTLE,
    .am_mask = EHOP_AM_FULL | EHOP_AM_HALF,       /* AM: two depths, keyed   */
    .pm_mode = EHOP_PM_PER_FRAME,                 /* PM steps every frame    */
    .fm_rate = 16, .fm_hop = 8,                   /* FM: 8..24 words/flip    */
    .pm_phase = 3, .pm_step = 5,
};
```

The rules, all checked by `ehop_cfg_check`:

- the width is 2, 4 or 8 bytes;
- the FM rate is between 1 and 4096 and inside the band's range;
- the hop is smaller than the rate;
- the phase and the step are each smaller than the rate;
- the AM depths are a non-empty subset of the band's allowed depths;
- the PM mode is one the band allows.

Sizes: the payload is at most 2048 bytes and each frame adds 48 bytes of
overhead.

## 8. Files and tests

| File | Contents |
|---|---|
| `kernel/src/ehop/ehop.h` | Public API. |
| `kernel/src/ehop/ehop_sched.c` | Band plan, configuration check, schedule, involutive apply, byte helpers. |
| `kernel/src/ehop/ehop_frame.c` | Channels, tags, seal/open, replay window, routing, QoS, router, hook. |
| `kernel/src/ehop/ehop_net.c` | Private networks: create, invite, join, rotate, remove, network channels. |
| `kernel/src/ehop/ehop_internal.h` | Shared helpers, plus the ML-DSA-65 prototypes repeated from `pq_security.h`. The test includes both headers, so a mismatch between the two is a compile error. |
| `kernel/src/ehop/test_ehop.c` | 187 host checks plus the throughput figures above. |

The module code is freestanding integer C11: no libc, no floating point, no
allocation, no 64-bit division, and fixed-size buffers. In cross-builds for
`aarch64-none-elf` and `riscv32-none-elf`, the only undefined symbols are
`shake256`, `sha3_256`, `chacha20_block`, `aead_seal`, `aead_open`,
`mlkem768_encaps`, `mlkem768_decaps` and `pq_mldsa65_{keygen,sign,verify}`.

The test suite covers:

- every AM depth: exact outputs and the involution, including partial words;
- whole-schedule involution: 7 configurations × 14 lengths × 6 frames;
- determinism across two nodes, and sensitivity to every input;
- the measured FM frequency and hop range;
- seal/open, replay handling and tampering at every byte class;
- wrong channel, wrong schedule and wrong epoch;
- tag routing among five channels;
- the band plan, routing fallback and QoS;
- two overlay routers talking through the hook;
- the full private-network lifecycle: join, rotate, remove, re-invite;
- ASan and UBSan clean.
