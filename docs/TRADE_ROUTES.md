# Trade routes: typed channels between ports, desks, stores and peers

A **trade route** is a declared, typed, one-way channel from an output port
to an input port. Routes connect:
- the desks inside the Cage (`docs/CAGE.md`);
- the store and its archive (`docs/REGISTRAR.md`, `docs/LEXICON.md`);
- the circuits on the canvas;
- this node and its peers.

Every route has a type, a capacity and a transit profile. Every frame it
carries is journaled.

Status: design, not built. Section 6 gives the build order.

## 1. The route manifest

A route is declared as data and compiled into the node's route table. The
manifest is hashed with SHA3-256; that hash is the route's id, so two nodes
that hold the same manifest agree on the id with no coordination.

```
[route.archives_to_hoard]
source        = "desk.archives.out"
target        = "hoard.in"
profile       = "inspected"          # bulk | signal | inspected | sealed
type          = "zxv/record:copybook-sha3:<hash>"
capacity      = 1024                 # frames, a power of two
on_both       = "arbitration"        # where a BOTH-tagged frame goes
on_neither    = "hold"
```

| Profile | For | Rule |
|---|---|---|
| bulk | Archives, batch files, large streams | Throughput first; every frame hashed and journaled |
| signal | Coordination, state updates, heartbeats | Small frames only; never queued behind bulk |
| inspected | Anything from outside | Every frame passes the Cage's checks for its type before delivery |
| sealed | Peer-to-peer private data | Encrypted end to end; relays see only opaque bytes |

## 2. Local routes: bounded ring buffers

A route between two parts of the same node is a single-producer,
single-consumer ring of fixed-size slots in shared memory. The structure is
C11 like the rest of the kernel:

```c
typedef struct {
    uint8_t  route_id[32];   /* SHA3-256 of the manifest                     */
    uint32_t type_tag;       /* index into the node's type table, checked at open */
    uint32_t capacity;       /* frames; a power of two                       */
    uint32_t frame_max;      /* bytes per slot                               */
    uint32_t pad;
    _Atomic uint64_t head;   /* next frame the producer writes (monotonic)   */
    _Atomic uint64_t tail;   /* next frame the consumer reads  (monotonic)   */
    uint64_t journaled;      /* frames written to the journal                */
} zrt_ring_t;

typedef struct {
    uint64_t seq;            /* equals the producer's head when written      */
    uint32_t len;            /* exact byte count, <= frame_max               */
    uint8_t  state;          /* lpres_state: NEITHER 0, TRUE 1, FALSE 2, BOTH 3 */
    uint8_t  pad[3];
    uint8_t  sha3[32];       /* of the payload                               */
    /* payload follows, len bytes */
} zrt_frame_t;
```

**Backpressure is exact.** The producer may write only when
`head - tail < capacity`. When the ring is full, the write is refused and
the producer waits or yields. A frame is never dropped and never
overwritten, so delivery order equals `seq` order, every time.

**State routing.** A frame tagged BOTH or NEITHER does not reach the target.
It goes where the manifest's `on_both` / `on_neither` says (the Arbitration
Desk, or held). The truth-state codes are `kernel/src/lpres/lpres.h`'s
`lpres_state` values (NEITHER 0, TRUE 1, FALSE 2, BOTH 3), not a new
numbering. The 64-state tag in `lpres_hex.h` orders its base differently;
a route carries the four-valued state, and a full tag only inside the payload.

**Sizes are byte counts.** A route reports exact integers (frames, bytes,
fill level as used/capacity). Nothing on a route is measured in floating
point or wall-clock rates.

## 3. Routes between nodes: a Vinea body, not a new wire

Vinea (`kernel/src/vinea/vna_wire.h`) already carries signed messages
between nodes:
- an ML-DSA-87 signature over every byte;
- the source and destination NodeIDs, each a full 32-byte SHA3-256 of the
  public key, never truncated;
- a sequence number, a timestamp window, a replay window and byte-exact
  deduplication;
- sessions encrypted with ML-KEM.

A trade-route frame between nodes is therefore one new Vinea message type
whose body is:

| Field | Bytes | Meaning |
|---|---|---|
| version | 2 | Route frame format, starting at 1 |
| profile | 1 | bulk 1, signal 2, inspected 3, sealed 4 |
| state | 1 | `lpres_state`: NEITHER 0, TRUE 1, FALSE 2, BOTH 3 |
| flags | 2 | bit 0 verification required, bit 1 compressed (deterministic encoding only), bit 2 sealed |
| route id | 32 | SHA3-256 of the manifest |
| route seq | 8 | Monotonic per route; gaps are visible, and a repeat is refused |
| type | 32 | SHA3-256 of the payload's schema |
| payload length | 4 | Exact bytes in this frame |
| payload | n | The typed data |

All integers are little-endian, like the rest of Vinea. The fixed part is
82 bytes.

Fields this body does **not** repeat, because Vinea's envelope already
carries and signs them: the magic, the source and destination NodeIDs, the
message sequence and the signature. One envelope, one signature: nothing
can be altered in the route frame without breaking the Vinea signature.

**Large payloads.** A Vinea message body is at most 65,535 bytes (its
length is a 16-bit field). A larger payload is split into frames with
consecutive route sequence numbers, or travels as a file by CID
(`vna_file.h`) with the route frame carrying the CID.

**Hashes.** The project uses SHA3-256 for identities and SHA-256 for
journals, so routes use those. A different hash (BLAKE3 or another) would
add a second implementation to verify and gains nothing here.

## 4. In the Rosetta Shell

Each dialect's spelling compiles to the same CLI-IR (`docs/ROSETTA_SHELL.md`):

| CLI-IR | Spelling |
|---|---|
| `route.open(manifest)` | `route open --from desk.archives.out --to hoard.in --profile inspected --type <t> --capacity 1024` |
| `route.status(id)` | `route status archives_to_hoard`: bindings, frames sent, journaled, fill level used/capacity, current state counts |
| `route.tap(id, n)` | `route tap archives_to_hoard --head 10`: copies of the next frames with their state tags; the route itself is not changed |
| `route.close(id, drain=true)` | `route close archives_to_hoard --drain`: stops new writes, delivers what is queued, journals the closing state |

Opening, tapping and closing are capability-checked like every other IR
operation. `route tap` needs read access to the route's type.

## 5. The Pit Boss on routes

The rulebook (`docs/PIT_BOSS.md`) watches routes and enforces their limits:
- A frame whose type hash does not match the route's is refused at the
  inlet, before its payload is parsed.
- A BOTH frame is diverted as its manifest says.
- A peer whose frames fail checks is recorded against its NodeID.
- Every route open, refusal, divert and close is one journal record.

## 6. Build order

| # | Slice | Acceptance test |
|---|---|---|
| 1 | `zrt_ring_t` with exact backpressure, plus frame hashing | A full ring refuses writes; under a randomised producer and consumer, delivery equals seq order with nothing lost (fuzzed) |
| 2 | Manifests: parse, hash to the route id, compile to the route table | The same manifest gives the same id on every CPU in `ai_cross` |
| 3 | State routing (on_both, on_neither) and journaling | A BOTH frame never reaches the target; every frame has a journal record |
| 4 | The Vinea route-frame message type | A tampered frame fails the Vinea signature; a replayed route seq is refused |
| 5 | Large payloads by chunk or CID | A 100 MB payload arrives byte-identical, with every chunk journaled |
| 6 | Rosetta Shell commands | Each spelling gives the same IR; a tap leaves the route's counters unchanged except the tap's own |
| 7 | The canvas: routes drawn as wires between ports | A route drawn on the canvas and one opened in the shell are the same route id |

## 7. Limits

- A route guarantees order, integrity and type at its ends. It does not make
  the payload's content true; the Cage's checks and the frame's state tag
  say what was checked.
- Routes between nodes depend on the peer staying online. A frame not
  acknowledged is re-sent; one past its route's retry limit is reported,
  never silently dropped.
