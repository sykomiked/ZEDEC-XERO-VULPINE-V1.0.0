# Wave 30 task: Linen (slot 363), Farm and forest care
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (093 understory), wave 12 (149 dibber), wave 14 (170 sheaf), wave 15 (192 coppice), wave 21 (255 glebe).
Manifest: `slot 363`, `layer L3`, `version 1.0.0`, `icon icons/l3/363-linen.svg`, `budget 233`, model `apps/linen_model.{h,c}`, blocks under kernel/src/apps/blocks/linen/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Tracks crops, soil, carbon and biodiversity across a farm or forest, and earns credits for verified care: fields (`place.list`, `place.add`), records (`files.read`, `files.write`), the books (`ledger.post`, `ledger.hold`, `ledger.holds`, `ledger.release`). Satellite imagery and soil sensors have no method: use only existing methods; list what's missing in your report.
Uses: `use dibber 1` (water per bed and growing degree days); `use sheaf 1` (farm accounts and "application allowed"); `use coppice 1` (carbon held and "felling allowed"); `use glebe 1` ("fits in storage" and crop sale value); `use understory 1` ("over budget"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `credit_ok`: (critical) "issue credits" (carbon gain measured on the ground AND by the second method within the declared margin AND verified by a named verifier) in Ada as Eve + APL as KRON witnessed
- `credits`: (critical) carbon credits (tonnes CO2e x1000 to credits, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `growth`: growth models (biomass gain per year by species table) in FORTRAN as Trebago
- `chem_ok`: chemical limits in Ada as Eve
- `fields`: field grid in APL as KRON
- `plots`: plot tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): satellite estimate 42 t and ground plots 31 t for one stand: the gain is BOTH; the test (scratch books) asserts state 3 and that credits wait on `ledger.hold`, never over-issued. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Credits are advisory until a verifier signs; issuing waits for the owner.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, ledger.read, ledger.post, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending ML credit_ok`; `pending ALGOL_60 credits`.
Now runs (Lingua front-end built): ALGOL 60 (twin Rootwyrm) for proofs of correctness: `logic algol60 <entry> apps/blocks/linen/<entry>.alg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): ML.
