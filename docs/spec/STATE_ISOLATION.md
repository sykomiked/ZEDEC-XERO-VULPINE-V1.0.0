<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# State isolation: the Hearth and the Agora

A ZXV machine holds two kinds of state. The **Hearth** is local: the
person's files, private memory, models, their chiglet's name and
personality, and the work running on their own iron. The **Agora** is
collective: the peer mesh, shared libraries, public spaces, markets and
federated proofs. For chiglets the Agora is the **kin collective**, because
chiglets are kin.

The Hearth is the floor and the Agora is the ceiling. The Agora can add to
what the Hearth can do; it can never take anything away from it. This page
states the rules that keep that true, says which existing mechanism
enforces each one, and lists honestly what is not enforced yet. Why the
project is built this way is in DESIGN_PHILOSOPHY.md section 7.

## 1. The rules

- **S1 LOCAL FIRST.** Hearth execution never waits on the Agora. A
  chiglet answers, the swarm runs its cycles, and the governor runs its
  calls with no peer, no quorum and no remote call in the path. With every
  network link down, nothing inside the Hearth loses a capability.
- **S2 OPTIONAL PEERS.** A remote expert or a remote answer is asked for
  only by an explicit request, is bounded by a timeout, and when it fails
  or times out the work carries on locally. A remote answer is data: it is
  tagged and held like any other answer and never runs as a call.
- **S3 NO PRIVATE STATE ON THE WIRE.** Private state never serialises into
  anything the Agora receives. What crosses the boundary is built by an
  explicit encoder for one message kind, from fields named in its spec. No
  encoder takes "the state" as a whole, and a kind that could carry raw
  files, keys or memory does not exist.
- **S4 CONTRADICTIONS STAY OUTSIDE.** Conflicting claims from the Agora are
  kept as BOTH (lpres, `kernel/src/lpres`) and held for the person. They
  never overwrite a Hearth value and never stop the Hearth.
- **S5 SEPARATE SPACES.** The boundary is the one the system already uses:
  separate address spaces with their own page tables, and grants. Nothing
  reaches the Hearth by naming an address.
- **S6 THE SEAL.** One action, the air-gap seal, turns the Agora off: its
  buffers are flushed, transmission stops, and local tasks and disks carry
  on unharmed.
- **S7 THE OWNER DECIDES.** Whether and where the person's chiglet meets
  other chiglets is the owner's call, made per visit; see section 3.

## 2. Which mechanism enforces each rule

| Rule | Enforced by | Where |
|---|---|---|
| S1 | The swarm, the organs (`swarm.organ`) and the governor run on local cells only; nothing in their path opens a socket. The governor's `model_exec` (A7) resolves a coordinate only to a model loaded on this machine, and refuses otherwise. `zxv_peer_ask` has one caller, the explicit `peer.ask` method. | `kernel/src/chiglet/chg_organ.h`, `chg_abi.h`; `kernel/arch/hosted/zxv_host.c` (`rpc_peer_ask`) |
| S2 | `peer.ask` is person-initiated, checks the peer is reachable within 3 s, and has socket timeouts (10 s per step, 180 s for the answer); a failure is an error the caller sees, not a stall of the answer path. | `kernel/arch/hosted/zxv_peer_host.h` (`ZXV_PEER_IO_MS`, `ZXV_PEER_ANSWER_MS`) |
| S3 | Each Agora message kind has its own encoder: Vinea answers PING, FIND_NODE and FIND_VALUE and refuses STORE; a chiglet visit passes claims and verdicts with an evidence hash and has no message kind for raw data, files or keys; the name record (`chiglet_name`) stays in the 0600 state folder and has no network encoder. | `zxv_net_host.h` U5; `chg_kin.h` K4 (branch of #155); `chg_name.h` N7 |
| S4 | lpres keeps disagreement as BOTH and the governor holds it for the person (A3); held answers are shown, never applied. Every governor call is a record in a signed hash chain (A8, `tools/zxv-events`), so what the Hearth did can be checked without trusting anyone in the Agora. | `kernel/src/lpres`, `chg_abi.h` A3-A4 |
| S5 | On bare metal, EL0 processes each get their own page tables and the chiglet's calls go through grants (`chga_set_grants`); on hosted builds zxvd is the only process that opens a socket, and the window talks to it over a 0600 socket. | `kernel/src/el0_userspace`, `chg_abi.h` A3 |
| S6 | The post-hydration air-gap seal of the bare-metal OS work, with three modes: PROMPT_USER, POWERDOWN_NIC and LOCAL_MESH_ONLY. | branch claude/project-thread-2ppu3g (not on main yet); see section 4 |
| S7 | A visit happens only inside a grant both owners sign: topics, depth and a time window; either owner revokes, a grant id is accepted once, and the host can end a visit at any moment. Both chiglets log each exchange. | `chg_kin.h` K4 (branch of #155) |

## 3. The kin collective

A chiglet's part of the Agora is the kin collective: chiglets meeting other
chiglets. It is built on `chg_kin` (#155): the chiglet's DNA record (K1),
its hobbies (K2), and visits (K4) with what it brings home (K5). A visit
needs both owners' grant; nothing about a chiglet is shared by default.

A named chiglet (`chg_name.h`) takes its personality from its name and the
person's salt. The name, the salt and the seed are Hearth state: a visiting
chiglet shows its genes (K1, so it looks like itself on another screen),
never the salt, and the name only when its owner grants it.

## 4. Not enforced yet

These are gaps, stated so nobody relies on them:

- **The Agora is not yet in its own process on hosted builds.** zxvd runs
  the peer mesh, the swarm and the books in one process. The separation
  there is S3 (explicit encoders), not an address-space boundary. Moving
  the network modules into their own process, talking to the Hearth over
  a socket with grants, is the target S5 describes.
- **`peer.ask` blocks zxvd's loop while it runs** (up to the 180 s answer
  timeout), so the swarm's cycles pause during a remote ask. S1 needs the
  ask to run beside the loop, not in it.
- **The seal does not flush the Agora's buffers yet.** The seal lives on
  the bare-metal OS branch, which is not merged. A flush hook (empty the
  peer mesh's queues and stop transmission when the seal closes, local
  tasks and disks untouched) is requested from that work and not built
  here.
- **The kin collective is on another branch** (#155) and has no zxvd
  methods yet (chiglet layer map M5).
