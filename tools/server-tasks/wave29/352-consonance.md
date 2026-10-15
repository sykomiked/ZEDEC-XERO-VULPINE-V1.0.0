# Wave 29 task: Consonance (slot 352), Agreements between groups
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 7 (047 sealwax), wave 13 (164 handfast), wave 14 (178 assize), wave 19 (240 isthmus), wave 20 (241 shallows).
Manifest: `slot 352`, `layer L3`, `version 1.0.0`, `icon icons/l3/352-consonance.svg`, `budget 233`, model `apps/consonance_model.{h,c}`, blocks under kernel/src/apps/blocks/consonance/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Negotiates, drafts and signs agreements between groups or nations: drafts in the Hoard (`files.read`, `files.write`), revisions on the journal (`journal.query`, `journal.verify`), signatures checked (`web4.verify`), translation (`xlate.translate`, `xlate.detect`), the negotiating room (`pspace.create`, `pspace.post`).
Uses: `use handfast 1` ("ready to sign", obligations, payment schedule); `use isthmus 1` ("next step allowed" and the settlement split); `use shallows 1` (latency limits and "show one meaning"); `use assize 1` ("filing accepted" and days left); `use sealwax 1` ("ready to sign" for each party's form). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `binding_ok`: (critical) "binding" (every party signed the same text version AND every clause reading agreed AND ratified by each party's declared rule) in Ada as Eve + APL as KRON witnessed
- `quota`: (critical) quota modelling (shares of a total by agreed weights, largest remainder, exact) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `obligations`: treaty obligations ledger (amounts and due dates, refusing on overflow) in COBOL as Sutra
- `clauses`: clause matrix (clauses x parties, four values) in APL as KRON
- `parties`: party hierarchy in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): party A reads clause 4 as a ceiling and party B as a floor: the clause is BOTH; the test asserts state 3, the clause held in escrow, and the rest of the agreement still standing until arbiters rule. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory: signing is each party's act; translation of legal text needs a human check.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, peers, net, ai.ask, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Eiffel binding_ok`; `pending Solidity obligations`.
Planned (no runtime yet): Eiffel, Solidity.
