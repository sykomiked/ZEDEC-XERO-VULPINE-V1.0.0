# Wave 31 task: Ostinato (slot 371), Circular economy
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (090 caravan), wave 12 (152 ratchet), wave 14 (171 mercer), wave 17 (208 cooper), wave 19 (234 neap).
Manifest: `slot 371`, `layer L3`, `version 1.0.0`, `icon icons/l3/371-ostinato.svg`, `budget 233`, model `apps/ostinato_model.{h,c}`, blocks under kernel/src/apps/blocks/ostinato/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Runs a circular economy: goods through reuse, repair and recycling (`net.offers`, `ledger.entries`), deposits held and returned (`ledger.hold`, `ledger.holds`, `ledger.release`, `ledger.post`), records and photos in the Hoard (`files.read`, `files.write`), parcels with Caravan's records.
Uses: `use cooper 1` ("next stage allowed" and the material ledger); `use mercer 1` (stock after a sale); `use ratchet 1` ("fault" and the next repair step); `use neap 1` ("entry may post"); `use caravan 1` (charged weight and "within carrier limits"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `refund_ok`: (critical) "refund the deposit" (item returned AND inspected AND not reported in use) in Ada as Eve + COBOL as Sutra witnessed
- `deposit`: (critical) deposit refunds (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `flow`: material flow model (grams in, reused, recycled, lost; conserved exactly) in FORTRAN as Trebago
- `hazard_ok`: hazard limits (batteries and chemicals routed only to declared handlers) in Ada as Eve
- `materials`: material grid in APL as KRON
- `lineage`: product lineage tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): an item is reported returned at the depot and in use by its holder: it is BOTH; the test (scratch books) asserts state 3 and the deposit escrowed until checked. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Refunds are human-approved; tests use scratch books.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: net, peers, ledger.read, ledger.post, fs.read, fs.write, camera, location.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Elixir hazard_ok`.
Planned (no runtime yet): Elixir.
