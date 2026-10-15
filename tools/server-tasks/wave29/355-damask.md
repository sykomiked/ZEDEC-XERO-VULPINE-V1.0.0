# Wave 29 task: Damask (slot 355), Restaurant kitchen
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (092 kettle), wave 13 (159 skillet), wave 14 (171 mercer, 172 escapement), wave 18 (226 swale).
Manifest: `slot 355`, `layer L3`, `version 1.0.0`, `icon icons/l3/355-damask.svg`, `budget 233`, model `apps/damask_model.{h,c}`, blocks under kernel/src/apps/blocks/damask/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Runs a restaurant kitchen: menus, recipes, orders, allergens and stock in the Hoard (`files.read`, `files.write`, `search.query`), supplier payments and sales in the books (`ledger.post`, `ledger.entries`, `ledger.balance`), tickets printed (`print.submit`), shift times (`sys.info`). Fridge sensors have no method: use only existing methods; list what's missing in your report.
Uses: `use skillet 1` (shopping list and "keep off the plate"); `use swale 1` (batch cost, "may sell this order", proofing); `use mercer 1` (stock after a sale and price from margin); `use escapement 1` ("job may run" and timesheets); `use kettle 1` ("contains an excluded allergen" and scaled quantities). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `serve_ok`: (critical) "may serve this dish" (label allergens and recipe allergens agree AND none excluded by the order) in Ada as Eve + MUMPS as MYRA witnessed (MYRA looks the recipe up in the allergen tree)
- `supplier_pay`: (critical) supplier payments (invoice lines in minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `portion`: portion costing (cost per plate from the scaled recipe) exact in FORTRAN as Trebago
- `fridge_ok`: fridge temperature limits (tenths of a degree, time out of range) in Ada as Eve
- `menu`: menu grid (dishes x services) in APL as KRON
- `allergens`: allergen tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a dish's label lists no nuts while its recipe includes almond flour: the dish is BOTH; the test asserts state 3 and that it is blocked from serving until resolved. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Allergen checks are advisory and never replace the chef's; payments wait for the owner; tests use scratch books.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, ledger.read, ledger.post, print, sys.read.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Laning_and_Zierler portion`.
Planned (no runtime yet): Laning and Zierler.
