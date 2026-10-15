# Wave 29 task: Caravel (slot 345), Trading stall
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 2 (079 fairground), wave 12 (150 pannier), wave 14 (171 mercer), wave 19 (234 neap), wave 20 (246 lode).
Manifest: `slot 345`, `layer L3`, `version 1.0.0`, `icon icons/l3/345-caravel.svg`, `budget 233`, model `apps/caravel_model.{h,c}`, blocks under kernel/src/apps/blocks/caravel/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Runs a trading stall or shop online and offline: stock and sales in the books (`ledger.post`, `ledger.balance`, `ledger.entries`), escrow (`ledger.hold`, `ledger.holds`, `ledger.release`), market listings (`net.offers`, `net.command` sell), parcels to buyers (`freight.pack`), receipts printed (`print.submit`).
Uses: `use mercer 1` (stock after a sale, price from cost and margin, "sale on credit allowed"); `use lode 1` (grade and the value recorded for an appraised item); `use neap 1` ("entry may post" and A = L + E); `use pannier 1` ("send now" and delivery receipts); `use fairground 1` (the 0.08889% fee and "offer allowed"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `release_ok`: (critical) "release the escrow" (buyer confirmed receipt AND seller confirmed dispatch AND the amount matches the listing) in Ada as Eve + COBOL as Sutra witnessed
- `sale_total`: (critical) sale total with the fee (price x qty plus 8889/10,000,000 of it, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `price_curve`: price curves (markdown by days in stock from a declared table) exact in FORTRAN as Trebago
- `stock_grid`: inventory grid (items x places) in APL as KRON
- `suppliers`: supplier tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the buyer's record says 3 items received and the seller's says 4 sent: the sale is BOTH; the test (scratch books) asserts state 3 and that the money stays on `ledger.hold` until both confirm. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Financial actions are human-approved; every post and release waits for the owner; tests use a scratch books file.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, ledger.post, peers, net, print, fs.read, fs.write.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Eiffel release_ok`; `pending Solidity sale_total`.
Planned (no runtime yet): Eiffel, Solidity.
