# Wave 30 task: Carrick (slot 360), Living contract
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 13 (164 handfast); wave 14 (178 assize); wave 17 (210 moraine); wave 19 (234 neap); wave 4 (047 sealwax).
Manifest: `slot 360`, `layer L3`, `version 1.0.0`, `icon icons/l3/360-carrick.svg`, `budget 233`, model `apps/carrick_model.{h,c}`, blocks under kernel/src/apps/blocks/carrick/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Binds multiparty commitments into one living contract: text and obligations in the Hoard (`files.read`, `files.write`), every revision on the journal (`journal.query`, `journal.verify`), signatures (`web4.verify`), payments in the books (`ledger.post`, `ledger.hold`, `ledger.holds`, `ledger.release`), keys in escrow (`recovery.status`).
Uses: `use handfast 1` ("ready to sign", obligations, payment schedule); `use assize 1` ("filing accepted" and days left); `use neap 1` ("entry may post" and A = L + E); `use moraine 1` ("release now" and the custody register); `use sealwax 1` ("ready to sign"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `limit_ok`: (critical) limit clauses (a payment or penalty at most the clause's cap AND inside its period) in Ada as Eve + COBOL as Sutra witnessed
- `penalty`: (critical) penalty schedules (days late x rate, capped, minor units) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `ledger`: obligation ledger (due, paid, held, refusing on overflow) in COBOL as Sutra
- `matrix`: obligation matrix (parties x obligations, four values) in APL as KRON
- `parties`: party tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): obligation 3 requires delivery by 1 May and obligation 7 forbids it before 10 May: the pair is BOTH; the test (scratch books) asserts state 3 and that only those obligations go to escrow while the rest of the contract stands. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Financial and legal actions are human-approved; each party signs; tests use scratch books.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, peers, ledger.read, ledger.post, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Eiffel limit_ok`; `pending Solidity ledger`.
Planned (no runtime yet): Eiffel, Solidity.
