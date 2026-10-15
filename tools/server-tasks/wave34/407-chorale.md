# Wave 34 task: Chorale (slot 407), Town governance
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (018 witness), wave 14 (178 assize), wave 19 (234 neap, 240 isthmus), wave 22 (271 cairn).
Manifest: `slot 407`, `layer L3`, `version 1.0.0`, `icon icons/l3/407-chorale.svg`, `budget 233`, model `apps/chorale_model.{h,c}`, blocks under kernel/src/apps/blocks/chorale/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Runs a cooperative's or town's governance: proposals and votes (`spaces.propose`, `spaces.vote`, `spaces.get`), debates (`social.post`, `social.feed`, `pspace.post`), budgets in the books (`ledger.accounts`, `ledger.entries`), published decisions (`aipi.put`, `web4.cid`), the record (`journal.query`, `journal.verify`).
Uses: `use cairn 1` (vote tallies and "decision carried"); `use assize 1` ("filing accepted" and the evidence matrix); `use isthmus 1` ("next step allowed"); `use neap 1` ("entry may post"); `use witness 1` ("chain intact"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `quorum_ok`: (critical) quorum limits (quorum met AND the motion's threshold AND the debate period over) in Ada as Eve + APL as KRON witnessed
- `budget`: (critical) public budget lines (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `turnout`: turnout model (turnout per ward per vote) exact in FORTRAN as Trebago
- `ballots`: ballot arrays in APL as KRON
- `decisions`: published decision layout (motion, result, votes) in APL as KRON
- `members`: member tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two motions passed on the same matter contradict: both stand as BOTH; the test asserts state 3 and that the assembly continues until a rule or vote resolves them. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Publishing a decision needs the chair's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, ledger.read, journal, fs.write, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Prolog quorum_ok`; `pending TeX decisions`.
Planned (no runtime yet): Prolog, TeX.
