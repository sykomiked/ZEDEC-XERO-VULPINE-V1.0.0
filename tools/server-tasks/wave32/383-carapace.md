# Wave 32 task: Carapace (slot 383), Fishing co-op
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (090 caravan), wave 13 (161 chandler), wave 14 (171 mercer), wave 15 (187 creel), wave 21 (258 atoll).
Manifest: `slot 383`, `layer L3`, `version 1.0.0`, `icon icons/l3/383-carapace.svg`, `budget 233`, model `apps/carapace_model.{h,c}`, blocks under kernel/src/apps/blocks/carapace/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Manages a fishing fleet or co-op: catch logs and quotas (`place.here`, `place.list`), landing payments in the books (`ledger.post`, `ledger.entries`, `ledger.hold`, `ledger.holds`, `ledger.release`), traceable labels printed (`print.submit`), market listings (`net.offers`).
Uses: `use creel 1` (quota left, "over quota", catch sales); `use chandler 1` (provisions and "within load limits"); `use mercer 1` (stock after a sale and price); `use atoll 1` ("may land this catch" and catch sale value); `use caravan 1` (charged weight and "within carrier limits"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `pay_ok`: (critical) "pay this landing" (log and landing weight agree within the declared margin AND quota left) in Ada as Eve + COBOL as Sutra witnessed
- `payment`: (critical) landing payments (kg x price per species, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `stocks`: fish stock models (landings and effort per season, catch per unit effort) in FORTRAN as Trebago
- `catch`: catch grid in APL as KRON
- `labels`: traceable label layout (vessel, area, date, species) in APL as KRON
- `vessels`: vessel tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the catch log says 410 kg and the landing scale 352 kg: the landing is BOTH; the test (scratch books) asserts state 3 and the payment held until explained. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Payments are human-approved; quotas are the authority's; tests use scratch books.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, ledger.read, ledger.post, print, net, peers, fs.read, fs.write.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Occam stocks`; `pending PostScript labels`.
Planned (no runtime yet): Occam, PostScript.
