<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# vinea-triage: store-and-forward triage records

vinea-triage is a C11 library for first responders who work where the
radio or data link drops in and out. A responder's device records triage
for each patient and packs the records into bundles. Each bundle is
signed with ML-DSA-87 and hash-chained to that responder's previous
bundle. Bundles may reach the receiving station late, out of order or
more than once. The station deduplicates and reorders them. When two
responders disagree about a patient, the station holds that patient as
**BOTH** for a person to decide. It never merges, averages or picks. The
API is [`include/zvtriage.h`](include/zvtriage.h).

**Advisory only.** It is not a medical device, and it is not
HIPAA-certified. **Bundles are signed but not encrypted.** See
[Privacy](#privacy) and [Limits](#limits).

## A triage record

| Field | Rule |
|---|---|
| patient | the triage-tag ID, 1..32 of `[A-Za-z0-9._:/-]` |
| system, category | `START` or `SALT`; `MINOR` (green), `DELAYED` (yellow), `IMMEDIATE` (red), `EXPECTANT` (gray, SALT only), `DEAD` (black). The category is a field the responder sets. The kit never computes it. |
| vitals | up to 8 integer vitals of distinct kinds. Each kind has one unit and a plausible range: respiratory rate /min 0..150, pulse /min 0..350, systolic mmHg 0..400, SpO2 % 0..100, capillary refill ms 0..20000, GCS 3..15, temperature in tenths of a degree C 150..460. A wrong unit or a value outside the range is `ZVT_E_RECORD`. |
| observed | the device clock, unix seconds |
| note | up to 200 bytes of well-formed UTF-8 with no control characters |
| based_on | the hash of the record (or decision) this one updates, or zeros |

## On the device: the outbox

- `zvt_outbox_record` validates a record, numbers it and stages it. It
  returns the record's hash.
- `zvt_outbox_seal` packs up to 32 staged records (at most 16 KiB) into the
  next bundle, chains it to the previous bundle and signs it.
- Bundles stay in the outbox (up to 256) until a signed acknowledgement
  from the station covers them. `zvt_outbox_bundle` gives any of them as
  bytes to send, or to send again.

The bundle layout (little-endian) is:

```
"ZVTB" u8 version(1) u8 encrypted(0) str responder u64 seq prev[32] u64 created
u16 n { u16 len, record } x n   ||   ML-DSA-87 signature
```

The signature covers `zind_subject("vinea-triage/bundle", body)` under the
context `"vinea-triage/bundle"`. A record's hash is
`zind_subject("vinea-triage/rec", record bytes)`. The test rebuilds a
bundle from this description and checks that it matches the outbox's
bytes exactly.

## At the station

`zvt_station_receive` classifies every bundle and returns one of these:

| The bundle | Result |
|---|---|
| the next one for its responder | applied, then any bundles waiting behind it (`ZXV_OK`); the receipt counts bundles, records, duplicates and new conflicts |
| already held, or already waiting | dropped (`ZIND_E_REPLAY`) |
| ahead of a missing one | kept until the gap is filled (`ZVT_E_GAP`); at most 32 per responder |
| a different bundle with a number already used, or not chained to the held history | a **fork**: a cloned device or key. The responder is held as BOTH, journaled and logged (`ZVT_E_FORK`), and later bundles from it get `ZIND_E_HELD`. A record number reused with other content is also a fork. |
| a bad signature | refused and logged as tamper (`ZIND_E_CHAIN`) |
| an unknown responder, a malformed bundle, or one that claims encryption | `ZVT_E_RESPONDER`, `ZXV_E_FORMAT`, `ZXV_E_NOTIMPL` |

A record the station already holds, arriving again in another bundle, is
counted as a duplicate and dropped. Records are applied all or nothing per
bundle.

### The patient picture: never merged

A patient's current picture is the set of records, and reviewers'
decisions, that nothing has superseded. A new record supersedes two
things: the record or decision named in its `based_on`, and its own
responder's earlier record for that patient. Everything else stands
beside it. A record that arrives after a later record already built on it
is kept but never becomes current.

| Current records say | State |
|---|---|
| one category | **TRUE**, that category |
| two or more categories | **BOTH**: held. Every current record is listed with its responder, and the category shown is none. |

A patient that becomes BOTH is logged once to the quarantine log. Vitals
from different records are never averaged. They stay attached to their
own records. Only the category is compared.

A registered reviewer, who is a person at the station, settles a held
patient by signing `zvt_resolve_message`. The message names the patient,
the hash of **every current record** and the category that stands. If
the current records change before the decision is applied, the decision
no longer verifies, so a reviewer can never settle a picture they did not
see.
`zvt_resolve` needs `ATTEST`. A forged decision is refused and logged.
The decision becomes the single current item, with phase Manifest. A
responder who builds on it (`based_on` = its hash) replaces it. A record
made without seeing it stands beside it and may hold the patient again.

### Acknowledgements and the journal

- `zvt_station_ack` signs what the station holds for a responder: the
  next sequence number and the head hash. `zvt_outbox_ack` checks the
  signature, and that the station's history matches the device's own
  (`ZIND_E_CHAIN` if it does not). Only then does it drop the covered
  bundles.
- The station's journal (`zvt_station_journal`, domain
  `"vinea-triage/station"`) is a libzind chain signed by the station key.
  It records every applied bundle, every decision (with the reviewer's
  full signature) and every fork. It verifies offline with
  `zind_chain_verify`.

## Privacy

libzxv exposes ML-KEM-1024 but no public symmetric cipher. This kit does
**not** build its own, so bundles are **not encrypted**. Anyone who can
see a bundle can read the patient IDs, categories, vitals and notes in
it. The signatures protect integrity and show who sent a bundle. They do
not give confidentiality. Encryption must be provided by the transport,
for example a VPN or TLS between the device and the station, or
link-layer encryption where the law and the radio service allow it.
Every bundle carries an "encrypted" byte that is always 0. A bundle with
any other value is refused with `ZXV_E_NOTIMPL`, never decrypted or
guessed at. If libzxv later exposes a public AEAD, encryption to the
station's ML-KEM-1024 key can be added behind that byte.

## Example

```c
zvt_outbox_create(cap, "MEDIC-1", medic_key, &ob);
zvt_outbox_record(ob, &record, hash);           /* validate, number, stage */
zvt_outbox_seal(ob, now, &seq);                 /* sign and chain a bundle */
zvt_outbox_bundle(ob, 0, buf, sizeof buf, &len);/* send it when the link is up */

zvt_station_receive(station, buf, len, &receipt);   /* any order, any number of times */
zvt_patient_state(station, "TAG-0142", &view);      /* TRUE / BOTH, categories, heads */
zvt_station_ack(station, "MEDIC-1", ack, sizeof ack, &alen);
zvt_outbox_ack(ob, station_pk, ack, alen);          /* prune what the station holds */
```

`make demo` runs [`examples/triage_demo.c`](examples/triage_demo.c), set at
a bus crash:

- Three bundles arrive out of order, one of them twice, and the station
  applies them in order.
- A corrupted copy is refused.
- Two medics disagree about one patient, who is held as BOTH. A doctor
  signs a decision, and a decision presented with another category is
  refused.
- An acknowledgement prunes a device.
- A cloned device is caught as a fork.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/vinea-triage test     # 816 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/vinea-triage SANITIZE=1 OUT=build/san test
make -C sdk/industry/vinea-triage fuzz FUZZ_ITERS=20000
```

`test_triage` covers these areas:

- **Records:** every system and category pair, and every vital at both
  ends of its range, one past each end, and in every unit. Notes with good
  UTF-8 are accepted. Overlong forms, surrogates, C1 controls, truncation
  and characters above U+10FFFF are refused.
- **Setup:** setup refusals and capacities (256 responders, 16
  reviewers).
- **Wire format:** an independent bundle builder must match the outbox
  byte for byte.
- **Delivery:** replays; four bundles delivered as 2, 3, 2, 0, 1;
  duplicates inside a later bundle.
- **Acknowledgements:** wrong key, wrong responder, tampered, truncated,
  and a history the device never made.
- **Hostile bundles:**
  - unknown responders and signatures by other keys;
  - an encrypted flag, malformed headers, 0 and 33 records;
  - a record by another responder, an invalid signed record, trailing
    bytes;
  - 64 flipped bits.
- **Forks:** a cloned device, a reused record number and a broken `prev`.
- **Conflicts and decisions:** conflicts held and never merged. Decisions
  that are forged, stale, carry the wrong category, come from an
  unregistered reviewer or lack `ATTEST` are refused, and a genuine one is
  applied. Late, already superseded records stay out of the picture.
- **Bounds and audit:** 32 records per bundle, 1,024 staged records, 256
  unacknowledged bundles, 32 bundles behind a gap and then draining.
  Journal kinds and the trusted key are checked, and the quarantine log
  verifies.

`fuzz_triage` runs four responders and six patients against an
independent model, which computes every picture from the whole history.
The link reorders, repeats and corrupts bundles. Records build on current
heads, on older or not yet delivered records, or on meaningless hashes.
Reviewers send genuine, forged and wrong-category decisions, and
acknowledgements prune the devices. After every operation, the status, the
receipt and every patient's state must match the model. The fuzzer makes
one random draw per statement, so gcc and clang print identical output.

## Limits

- **Advisory, not a medical device.** vinea-triage records what
  responders entered and shows where they disagree. It does not triage,
  diagnose or recommend treatment. Its ranges reject impossible values,
  not wrong ones. It is not certified under any medical-device,
  EMS-software or health-data regulation, and it is **not
  HIPAA-certified**. Whoever deploys it is responsible for those
  obligations.
- **No encryption** (see [Privacy](#privacy)). Patient data in a bundle is
  readable in transit and at rest unless the transport and storage
  encrypt it.
- **No settlement or payment over amateur radio,** and no claim that
  amateur (ham) bands may carry this traffic. In many countries amateur
  radio forbids encrypted content and commercial or business traffic.
  Check the rules for any radio service before use.
- **Clocks and people are trusted as reported.** `observed` is the
  device's clock, and the kit does not correct it. A signature proves
  which device key signed, not who was holding the device, or that the
  patient was assessed correctly.
- **A fork holds a responder for good** in this version. Re-admitting a
  device, by registering a new key under a new ID, is an operator's
  decision made outside the kit.
- Everything is in memory: up to 65,536 patients, 65,536 records and
  65,536 bundles per station. Persistence and replication are up to the
  application. The journal export (`zind_chain_export`) can be stored
  anywhere. Each bundle carries a 4,627-byte signature, which is large
  for slow links. Batch records into bundles (up to 32 per bundle).
- START and SALT are named only as category schemes. No organization
  behind them has endorsed this kit.
