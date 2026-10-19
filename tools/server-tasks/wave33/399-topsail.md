# Wave 33 task: Topsail (slot 399), Personal cloud
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 15 (189 flue); wave 18 (225 tributary); wave 22 (267 holm, 266 thaw); wave 7 (087 belvedere).
Manifest: `slot 399`, `layer L3`, `version 1.0.0`, `icon icons/l3/399-topsail.svg`, `budget 233`, model `apps/topsail_model.{h,c}`, blocks under kernel/src/apps/blocks/topsail/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Hosts apps and files on your own devices and trusted peers as one personal cloud: sync (`spaces.list`, `files.cross`, `backup.run`, `backup.list`), devices and peers (`devmesh.devices`, `net.peers`, `peer.status`), the cloud top (`spaces.get`, `spaces.set_cid`, `aipi.put`, `aipi.get`), budgets (`power.budget`).
Uses: `use flue 1` (version vector compare and "sync fits"); `use tributary 1` (agreeing answers); `use holm 1` ("may leave hardened mode"); `use thaw 1` ("may restore this item" and restore byte totals); `use belvedere 1` ("may this file go on the cloud top"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `cap_ok`: (critical) resource caps (each tenant within its storage and compute share) in Ada as Eve + APL as KRON witnessed
- `capacity`: (critical) capacity model (bytes and replicas per device, headroom) in FORTRAN as Trebago + APL as KRON witnessed
- `failover`: failover choice (next replica by health and recency) in Ada as Eve
- `replicas`: replica grid in APL as KRON
- `ledger`: hosting ledger (refusing on overflow) in COBOL as Sutra
- `tenants`: tenant tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two replicas diverge after a network split: the file is BOTH; the test asserts state 3 and that both are kept until reconciled. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Nothing goes on the cloud top without the owner's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, backup, peers, net, sys.read.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Gleam failover`; `pending Elixir replicas`; `pending Nim capacity`.
Planned (no runtime yet): Gleam, Elixir, Nim.
