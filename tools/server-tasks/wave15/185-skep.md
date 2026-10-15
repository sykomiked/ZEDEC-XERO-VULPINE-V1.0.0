# Wave 15 task: Skep (slot 185), Swarm coordination
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (061 murmuration), wave 1 (029 trellis), wave 4 (084 forage), wave 8 (077 trickle).
Manifest: `slot 185`, `layer L2`, `icon icons/l2/185-skep.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: coordinates a swarm of agents and nodes on shared work (`swarm.state`, `swarm.earnings`, `net.peers`, `peer.status`) with bounties and patronage grants from the books (`ledger.balance`).
Uses: `use trellis 1` (peers per state, peer record); `use murmuration 1` (the swarm_shares block it uses: cycle share, busiest, within limits); `use trickle 1` (amount due at a tick, "stream allowed"); `use forage 1` (bounty value net of the fee, "claim fits", ranking). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): grant ledger (each contributor's grant by weight, largest remainder, summing exactly to the pool, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; task placement (tasks to nodes by spare capacity, largest remainder) in FORTRAN as Trebago + APL as KRON witnessed; "within the agent's budget" in Ada as Eve + APL as KRON witnessed; task matrix in APL as KRON; colony tree in MUMPS as MYRA.
BOTH test: agents 1 and 2 return result ids 7 and 9 for one task: the result is BOTH, the task runs again on a third agent, and the grant for it is held until a majority agrees.
Financial: grants are posted only with the owner's approval.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: swarm, peers, net, ledger.read, ledger.post, notify.
Planned (no runtime yet): Mojo, Occam, Gleam.
