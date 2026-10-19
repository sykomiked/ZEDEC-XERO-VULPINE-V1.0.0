# Wave 30 task: Fanfare (slot 364), Recognition and credit
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (156 crier); wave 18 (221 watershed); wave 19 (234 neap); wave 22 (271 cairn); wave 6 (080 ninefold).
Manifest: `slot 364`, `layer L3`, `version 1.0.0`, `icon icons/l3/364-fanfare.svg`, `budget 233`, model `apps/fanfare_model.{h,c}`, blocks under kernel/src/apps/blocks/fanfare/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Recognises contributions in a team or community with badges, credits and an auditable reputation: the circle (`social.feed`, `social.post`), votes (`spaces.propose`, `spaces.vote`), credits in the books (`ledger.post`, `ledger.hold`, `ledger.holds`, `ledger.release`), announcements (`chat.post`, `pspace.post`).
Uses: `use watershed 1` (member shares, "spend allowed"); `use neap 1` ("entry may post"); `use cairn 1` (vote tallies and "decision carried"); `use crier 1` ("may broadcast to this channel"); `use ninefold 1` (holding per form and share x100 per form). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `gaming_ok`: (critical) anti-gaming limits (self-award refused, at most n awards from one giver a week, reciprocal pairs flagged) in Ada as Eve + APL as KRON witnessed
- `reputation`: (critical) reputation score (contributions weighted by kind, decaying by a declared half-life table) in FORTRAN as Trebago + APL as KRON witnessed
- `credits`: credit ledger (minor units, refusing on overflow) in COBOL as Sutra
- `report`: public contribution report (counts per member per kind) in APL as KRON
- `members`: member tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a contribution claimed by two members: it is BOTH; the test (scratch books) asserts state 3 and that its credit is held, neither given nor denied. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Credits give no priority or discount; awards are human-approved.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, ledger.read, ledger.post, swarm, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending PHP report`.
Planned (no runtime yet): PHP.
