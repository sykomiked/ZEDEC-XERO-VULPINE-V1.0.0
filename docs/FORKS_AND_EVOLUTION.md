<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Forks and Evolution

ZXV has no "next version". Nobody ships a ZXV 2.0 that everyone has to move to. The system changes the way a living thing does: people change their own copies, share what they made, and other people choose what to take. Forking is meant to make the system better. A fork adds a new choice, and it does not split the network.

This guide covers what that means for you as a user, and how it works for developers. The code is in `kernel/src/evolve/`. The public header is `evo.h`.

## Contents

1. The short version
2. For users
3. How compatibility works without version numbers
4. Forks and lineage
5. Choosing what to trust
6. Seeing what people actually run
7. The safety core: what every fork must keep
8. Device profiles
9. For developers: making a fork that plays well
10. Tests
11. Honest limits

## 1. The short version

- **Compatibility comes from capabilities, not versions.** Every message type, file format and module interface is described by a small schema. Each schema has a content address (a CID). Two devices compare what they can do and use the part they share.
- **What a device does not understand, it keeps.** If your friend's fork adds a field to chat messages, your device passes that field along unchanged. It does not drop it and it does not choke on it.
- **A fork is a signed record.** The record names the fork's parents, its author's key and what it changed. Forks form a family tree (a DAG). You can label your build anything you like, such as "v7", "mia-garden" or nothing at all. The system never relies on the label.
- **You choose whom to trust.** You can trust a publisher, pin a build, or rate a publisher or build from 1 to 5 stars. You use the same trust list as for updates.
- **Money paths are protected.** A small safety core covers crypto, the ledger rules, the no-usury rule, the phi% tithe and consent. A conformance check pins that core. A fork that fails the check can still chat, share files and run apps with everyone, but it cannot move money with anyone.

## 2. For users

**Getting new things.** Modules, models and settings reach you from peers. Each one is a signed, content-addressed object. Nothing installs itself. You see what is on offer and who made it. You also see roughly how many people run it and how you or people you trust rated it. Then you decide.

**Making your own build.** Take any build, swap a module, add one, or remove one, then publish. Your build is now a fork, with your key on it and its parents recorded. Anyone who trusts you, or who pins your build, can run it. If you combine two builds that changed the same thing in different ways, the system asks you which one to keep. It never picks for you.

**Mixing.** You can run the chat module from Alice's fork, the games from Bob's and the ledger from upstream on the same device. Your device checks that each module's needs are met by something else you run (section 8).

**Labels and versions are yours.** Call your build "3.0", "2026-10-09", "summer" or nothing. Two people can both have a "v2" and nothing breaks. The system tells builds apart by their CIDs, not by their labels.

**Privacy.** Counting who runs what is opt-in. If you opt in, your device adds one anonymous token per period. A fresh random secret makes the token, and your device then forgets the secret. Nobody can link your tokens across periods or variants, or link them back to you.

## 3. How compatibility works without version numbers

### Capability descriptors (C1)

A descriptor has three parts:

- a **family name**, such as `chat.message`, `zxpkg.manifest` or `pay.request`;
- a **kind**: message, file format or module interface;
- a **class**: general or money.

It also has a list of **fields**. Each field has:

- a numeric tag;
- a type: unsigned integer, bytes, text or CID;
- a maximum length;
- a default, which is used when the field is absent;
- two flags: *required*, and *must-understand*.

The descriptor's identity is the CID (CIDv1, raw, sha3-256) of its canonical bytes. Field names are not part of a descriptor, so renaming a field breaks nobody.

### Negotiation (C2)

Each device advertises a capability set. For each family, the set names the descriptor CID the device uses. When two devices meet, they handle each family they have in common like this:

1. If the CIDs match, they use that descriptor.
2. If the CIDs differ, each device fetches the other's descriptor by CID. Content addressing makes the fetch self-certifying, because bytes that do not hash to the CID are refused.
3. Each device computes the **core**: the fields that both descriptors define with the same type and the same default. Both devices compute the same core and the same core CID independently.
4. A field that is required on one side but unknown to the other makes the family incompatible for that pair. All other families still work.

### Messages (C3, C4)

On the wire, each field is written as tag, type, length and value, in ascending tag order.

- **Forward compatible:** the receiver keeps fields it does not know and writes them back out unchanged. If a relay knows only the base schema, it still forwards a message from a richer fork byte for byte.
- **Backward compatible:** if a known optional field is absent, the receiver uses its declared default.
- **Must-understand:** this flag travels on the wire. If a receiver gets a field marked must-understand that it does not know, it rejects the whole message rather than guessing. A sender also refuses to send such a field to a peer whose core lacks it, so the application learns before anything is sent. Use the flag sparingly: only when ignoring a field would be dangerous, such as a field that changes what an amount means.

## 4. Forks and lineage

A **fork record** contains:

- 0 to 4 **parent** record CIDs;
- the **author**'s key id (SHA3-256 of their public key);
- a free-text **label**;
- a **stamp** the author chooses;
- up to 8 **changes** to named slots: `ADD` (the slot must be empty), `REPLACE old -> new`, or `REMOVE old`.

A slot's value is a CID. It can point at a descriptor, a module manifest, a model file or a profile. The record's CID is the CID of its canonical body. The author signs the body. Lineage records are long-lived anchors, so sign them at the pq_matrix release level (`PQM_SIG_PURPOSE_RELEASE`, which is MATRIX: ML-DSA-87 plus SLH-DSA-SHAKE-256s). The DAG accepts any signer the verify hook accepts.

**State is computed, not stored (F3).** For each slot, the system looks at every write to that slot in a build's ancestry. The *heads* are the writes that no later write in that ancestry overrides. If there is one head value, that value is the slot's value. If there are different head values, the slot is in conflict.

**Acceptance (F4).** A record joins your DAG only if all of these hold:

- its parents are already in your DAG;
- its signature verifies, and the key matches its author field;
- each change applies to the merged state of its parents;
- it resolves every conflict that merging those parents creates.

So every accepted build has a well-defined state. Any two lineages can always be merged by a record that says how. `evo_dag_conflicts()` tells the author which slots need a decision. The result does not depend on the order in which records arrived. The tests replay random DAGs in a different order and get identical states.

## 5. Choosing what to trust

`evo_trust_t` decides whether a build is adoptable for **you**:

- **Publishers.** You can reuse one of two existing lists, so you keep a single trust list:
  - the update module's opt-in publisher list (`evo_trust_upd`, which binds `upd_is_trusted` in `kernel/src/update/update.h`);
  - zx_upcheck's per-bucket ML-DSA-65 release keys (`evo_trust_zxu`). A publisher counts as trusted if any enabled bucket trusts its key.
- **Pins.** Pinning a record endorses that record and its whole ancestry, without trusting the author for anything else.
- **Ratings.** You can rate a publisher or a record from 1 to 5 stars. With `min_stars` set, anything rated below that is blocked, and so is everything descended from a blocked record.

A build is adoptable when every record in its ancestry is either endorsed by a pin or authored by a trusted, unblocked publisher, and none of them is blocked.

## 6. Seeing what people actually run

Each variant has an adoption sketch for each epoch. The sketch is a k-minimum-values sketch with k = 64.

- A device that opts in contributes one token per epoch. The token is `SHA3-256("ZXV-EVO-adopt" || fresh secret || variant || epoch)`.
- Sketches merge by union, and merging is idempotent. Gossip that repeats a sketch therefore never counts anyone twice.
- The count is exact below 64 devices. Above that the estimate is about ±13%.

These counts show which variants are thriving. They are hints, not votes: see the limits in section 11.

## 7. The safety core: what every fork must keep

`evo_conform` holds a pinned suite (`evo_conform_suite_cid`) covering these checks:

| check | rule |
|---|---|
| `HASH` | SHA3-256 matches the FIPS 202 vectors and this build's own SHA3 |
| `TITHE` | `floor((a + isqrt(5a²)) / 200)`, exact for every 64-bit a |
| `LEDGER` | the sum of debits equals the sum of credits, and equity = debit − credit on every line (rails DEBIT 555 / CREDIT 777 / EQUITY 888) |
| `USURY` | the amount due always equals the principal, however much time has passed |
| `CONSENT` | a spend is allowed only if consent was granted, the amount is within the granted maximum, and the consent has not expired |

The suite checks **rules**, not someone's implementation. For example, the tithe check does not compute a square root. It confirms `200t − a ≤ a√5 < 200(t+1) − a` by comparing squares in 192-bit integers. Your fork can compute the tithe however it likes, as long as the answers are exact.

**Checking a peer.** The verifier sends a fresh random 32-byte seed. The peer expands the seed into inputs, runs its own code on them and returns the answers. The verifier checks each answer against the rules. Answers recorded for an earlier seed do not count. `evo_session_gate()` then allows a session's **money-class** capabilities only if both sides pass every check. General capabilities stay available either way.

## 8. Device profiles

A **module manifest** names three things:

- the code blob's CID;
- the descriptors the module provides;
- the descriptors it requires.

A **profile** is a list of entries. Each entry names:

- a manifest;
- the fork record it came from;
- where it runs: `LOCAL`, `PEER` (with the key id of the device that runs it) or `OFF`.

A profile has a canonical encoding and a CID, so you can share your whole setup as one CID.

`evo_profile_check()` confirms that every requirement of every module that is switched on is satisfied by something another module provides. A requirement is satisfied when the provider understands every field the requirement uses (`evo_desc_satisfies`), so a module from one fork can satisfy a module from another. If you pass a DAG, the check also confirms that each module really is in the build its entry names.

## 9. For developers: making a fork that plays well

1. **Extend, don't rename.** To add data to an existing family, add **optional** fields. Choose their tags with `evo_ext_tag("yourname.feature")`, which gives tags from 0x1000 to 0x7fff. Leave the base fields alone.
2. **Required fields split the network.** Adding a required field means peers that lack it cannot use that family with you. Prefer an optional field with a sensible default.
3. **Must-understand is for danger.** Use it only when a peer ignoring the field would do harm.
4. **A new meaning needs a new family.** If you change what an existing field means, use a new family name such as `chat.message.alice`. Structural checks cannot detect a change in meaning (see the limits).
5. **Publish a fork record** for your build. Sign it with your release key and announce its CID. Your users can then pin it.
6. **Keep the safety core.** Run `evo_conform_local()` on your build. If it does not return `EVO_CHK_ALL`, your money paths will not work with anyone, though everything else still will.

Example of negotiating and sending:

```c
evo_negotiate(&reg, &mine, &theirs, &sess);         /* fetch sess.need[] if any, retry */
evo_session_gate(&sess, my_conform, peer_conform);
const evo_sess_cap_t *chat = evo_session_find(&sess, "chat.message");
if (chat && chat->allowed)
    evo_msg_encode(&msg, &chat->core, out, sizeof(out), &len);
```

## 10. Tests

`kernel/src/evolve/test_evolve.c` contains 5250 checks. The recipe is in the evolve verify-all lines.

- **Random fork pairs:** 400 pairs, each a random base plus random extensions, some with colliding tags. Every pair agrees on the same core CID and exchanges messages on the shared core. Each untouched message comes back byte-identical, so every extension survives. A must-understand extension is refused at send time.
- **Passthrough:** a relay that knows only the base edits one field and forwards the message. The richer fork gets every extension back.
- **Strictness and fuzzing:** missing required fields, non-minimal integers, out-of-order tags, truncation, and 20,000 fuzzed inputs. Every input the decoder accepts re-encodes to exactly the same bytes.
- **Lineage:** records are signed with pq_matrix (STANDARD and MATRIX levels). The tests cover merges, conflicts, how conflicts are resolved, delete-versus-modify, bad signatures, an author field that does not match the key, unknown parents, and 40-record random DAGs replayed in another order.
- **Trust:** the tests cover the update module's list, zx_upcheck buckets, pins, ratings and blocking.
- **Adoption:** opt-in only. The count is exact below k. Merging is idempotent. The estimate for 3000 devices falls within the expected range.
- **Conformance:** the tithe checker is cross-checked against `pay_tithe_phi` on 100,000 random amounts plus edge cases. Six broken forks each fail exactly their own rule, including a 61.8% tithe, a tithe that rounds up, a little interest, and a consent check that ignores expiry. Gating removes only the money capabilities.
- **Profiles:** modules from different forks are mixed. The tests also cover an unsatisfied requirement and a module claimed from a build that does not contain it.

## 11. Honest limits

- **Same structure does not mean same meaning.** If two forks give the same tag and type different meanings, each will read the other's values its own way. `evo_ext_tag` makes accidental collisions unlikely (about 1 in 28,000 per pair of extensions), not impossible.
- **Conformance is a spot check, not a proof.** It tests the code a peer runs when challenged. Money paths should still check each real value, for example by calling `evo_tithe_is_exact` on every payment.
- **Adoption counts can be gamed.** Because they are anonymous, anyone can inflate them. Treat them as a popularity hint, never as a vote or a security signal.
- **This module holds no keys and moves no bytes.** Signatures go through a verify hook. Descriptors, records, sketches and challenge responses are byte strings that the caller carries over Vinea or ipfs_node.
- **Capacities are fixed:**
  - 32 fields per descriptor;
  - 32 families per session;
  - 64 records in a DAG;
  - 8 changes per record;
  - 24 entries per profile.

  Going over a limit is an error. Nothing is ever silently truncated.
- **Some functions are not reentrant.** The conformance functions and `evo_profile_check` use static scratch memory.
