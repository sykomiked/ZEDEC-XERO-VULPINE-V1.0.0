# Public spaces and the watch hall

A **space** is a place people make on the peer network: a room for talk, or a
**watch hall** where everyone sees the same moment of the same video. Nobody's
server holds it. The devices of whoever is in a space host it, and the space
lasts as long as one of them is online.

Code:

- `kernel/src/vinea/vna_space.{h,c}`: the protocol, a pure state machine (rules P1 to P7).
- `kernel/arch/hosted/zxv_net_host.h` U14: the desktop node carries it.
- `kernel/arch/hosted/zxvd_pspace.{h,c}`: the `pspace.*` and `hall.*` methods of [zxvd](ZXVD.md),
  a method group behind the feature switch `pspace` (rules W1 to W6).

## How a space works

1. **Anchor (P1).** A space starts as a genesis block. The block holds:
   - the kind (room or hall);
   - the governance (owner, or quorum with a percentage);
   - a name and an optional directory topic;
   - the creator's NodeID, the creation time and a random nonce.

   The space's **CID** is the IPFS CIDv1 (raw, sha2-256) of those exact
   bytes. Sharing the CID is sharing the space, and anyone holding the block
   can check it against the CID.
2. **Hosted by visitors (P2).** Every member publishes a signed host record
   in the Vinea DHT, under one of 8 keys derived from the CID. The record
   holds the genesis block and the member's address. Someone with only the
   CID looks those keys up until a host answers, then joins through it.

   A record's key must be one its own genesis gives, so nobody can plant
   records under another space's keys. Each desktop node also holds up to 32
   other spaces' records for the DHT, even when it is in none
   (`zxv_net_space_store`).
3. **Directory (P3).** A space with a topic is also listed under one of 16
   directory keys for that topic, and `pspace.find` walks them. The DHT keeps
   one value per key, so a search shows at most 16 spaces per topic at a
   time.
4. **Members (P4).** The host answers a JOIN with a WELCOME: the genesis
   block (checked against the CID) and up to 16 members with their addresses.
   Then it sends the current playback state and the 8 most recent posts.

   Members say PRESENCE every 30 s, and one silent for 100 s is dropped.
   Every message is a signed Vinea message (ML-DSA-87), so a member is who it
   says it is.
5. **Events (P5).** A post or a playback change is a record signed by its
   author, passed on unchanged. The author sends it to every member it knows,
   and each member passes a new one to up to 4 others. Each event is applied
   once.

   There is no delete, ban or silence operation. Nobody can remove anyone's
   speech.

   Event records are never stored in the DHT; only members carry them.
6. **Watch hall (P6).** The playback state is one event holding:
   - the media CID;
   - stopped, paused or playing;
   - a position at the author's wall-clock time;
   - a rate (65536 = normal speed, up to 4x).

   Every viewer computes the same playhead from it:
   `pos + (now - at) * rate / 65536`. So the group stays in step with no
   clock server.

   The latest change wins, by Lamport time, then author. In an **owner**
   space only the creator steers playback (vertical). In a **quorum** space
   any member does (horizontal).
7. **Bounded (P7).** The tables are fixed:
   - 8 spaces per node;
   - 32 members and the last 32 events per space;
   - a ring of 256 event hashes for dedupe.

## From the app (zxvd)

Networking must be on (`net.command` `lan` or `online`).

The methods are off until the owner turns them on: start zxvd with
`--enable pspace`, or call `features.set` with `{"feature": "pspace"}`.
Reading and leaving (`pspace.leave`, `pspace.events`, `pspace.members`,
`pspace.list`, `hall.status`) answer with the switch off.

```sh
call() { printf '%s\n' "$1" | nc -U "$HOME/.zxv/run/zxvd.sock"; }
call '{"jsonrpc":"2.0","id":1,"method":"pspace.create","params":{"kind":"hall","gov":"owner","name":"Movie night","topic":"film"}}'
# {"cid":"bafkrei..."}  share this CID
call '{"jsonrpc":"2.0","id":2,"method":"hall.load","params":{"cid":"bafkrei...","media":"bafybei..."}}'
call '{"jsonrpc":"2.0","id":3,"method":"hall.play","params":{"cid":"bafkrei..."}}'
# on another machine:
call '{"jsonrpc":"2.0","id":1,"method":"pspace.join","params":{"cid":"bafkrei..."}}'
call '{"jsonrpc":"2.0","id":2,"method":"hall.status","params":{"cid":"bafkrei..."}}'
# {"state":"play","pos_ms":...,"rate_q16":65536,"may_control":false,...}
```

| Method | Params | Result |
|---|---|---|
| `pspace.create` | `kind` (room, hall), `gov` (owner, quorum), `quorum_pct`? (default 51), `name`, `topic`? | `cid` |
| `pspace.find` | `topic` | `topic`, `done`, `found` (call again until `done`) |
| `pspace.join` / `pspace.leave` | `cid` | `cid`, `phase` / `left` |
| `pspace.post` | `cid`, `text` (1 to 1024 bytes) | `posted` |
| `pspace.events` | `cid`, `since`? | `events`, `next` |
| `pspace.members` | `cid` | `members` (`id`, `addr`, `last_seen_ms`), `count` |
| `pspace.list` | | `online`, `spaces` (each with its genesis, `phase`, `members`, and `hall` for a hall) |
| `hall.load` | `cid`, `media`, `pos_ms`? | the `hall.status` object |
| `hall.play` | `cid`, `pos_ms`?, `rate_q16`? | the `hall.status` object |
| `hall.pause` | `cid`, `pos_ms`? | the `hall.status` object |
| `hall.seek` | `cid`, `pos_ms` | the `hall.status` object |
| `hall.status` | `cid` | `set`, `state`, `media`, `pos_ms`, `base_pos_ms`, `at_ms`, `rate_q16`, `now_ms`, `viewers`, `may_control`, `author` |

Bad parameters answer -32602; with the switch off the other methods answer
-32012. Refusals answer error -32030, with a message that says why. Examples: the
network is off, not in that space, only the hall's owner controls playback,
nothing loaded.

`hall.play` and `hall.pause` without `pos_ms` continue from where every viewer
is now. `hall.seek` keeps the state and the rate.

## Honest limits

- **This is the control plane only.** The hall says what to play and where
  the playhead is. Delivering the video, with every viewer serving the
  pieces it holds so capacity grows with viewers (`src/stream/stream_swarm.h`),
  is the next step. It needs Vinea's encrypted sessions (`vna_session.h`)
  between members.
- Viewers whose wall clocks disagree see the playhead offset by the
  difference.
- A search shows at most 16 spaces per topic. A space larger than 32 members
  per node relies on gossip forwarding.
- Posts are kept in memory only (the last 32 per space). History is lost
  when every member leaves.
- Group playback speed is the hall's rate, the same for everyone. "Faster
  for everyone" in the sense of delivery comes with the data plane above.

## Tests

- `make test-vinea-space` (in `verify-all`). Seven simulated nodes, with
  ASan and UBSan, check:
  - CID and keys;
  - join by CID;
  - posts and the synced playhead;
  - newcomer catch-up;
  - dedupe, forged and tampered events, stray WELCOMEs;
  - quorum control and the directory;
  - the space outliving its creator;
  - silent members dropped;
  - key binding, event records never stored;
  - bounded tables.
- `kernel/arch/hosted/test_hosted.sh` (net host [8]): two real UDP hosts on
  127.0.0.1. One makes a hall, the other joins by CID; they exchange a post
  and compute the same playhead.
- `kernel/arch/hosted/test_zxvd_pspace.sh`: the switch, every `pspace.*` /
  `hall.*` parameter rule, and two real net hosts on 127.0.0.1 sharing a
  hall through the methods (the owner steers, the visitor is refused, posts
  with invalid UTF-8 reach the other side as U+FFFD, the directory finds the
  hall). `test_zxvd.py` drives the methods end to end on the daemon.
