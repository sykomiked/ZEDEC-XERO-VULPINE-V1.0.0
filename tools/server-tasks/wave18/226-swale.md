# Wave 18 task: Swale (slot 226), Small bakery
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (109 tally), wave 4 (078 tallystick), wave 8 (082 larder, 092 hearth).
Manifest: `slot 226`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/226-swale.svg`, `budget 233`, model `apps/swale_model.{h,c}`, blocks under kernel/src/apps/blocks/swale/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Runs a small bakery: recipes and orders in the Hoard (`files.read`, `files.write`), sales and receipts in the books (`ledger.accounts`, `ledger.entries`, `ledger.post`), statements (`iso20022.render`), receipts printed (`print.submit`).
Uses: `use hearth 1` ("contains an excluded allergen" and scaled quantities); `use tallystick 1` (invoice total, "paid in full", numbering); `use tally 1` (rational arithmetic and currency amounts); `use larder 1` (spent per category and "over budget"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) batch cost (ingredient qty x unit price, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- (critical) "may sell this order" (the order's excluded allergens and the recipe's allergens share none) in Ada as Eve + MUMPS as MYRA witnessed (MYRA looks the recipe's ingredients up in Hearth's allergen index)
- dough and proofing model (hydration x100 = water x 10000 / flour; proof minutes from a dough-temperature table) exact in FORTRAN as Trebago
- batch grid (orders x products, the batches to bake) in APL as KRON
- recipe tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the stock count says 12 kg of flour while the sales log implies 4 kg left, giving BOTH; the test asserts state 3, the stock held (not zeroed) until a recount, and the orders that need flour flagged. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Allergen flags are advisory; the app cannot vouch for product labels. Every post waits for the owner's confirmation; tests use a scratch books file.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, ledger.read, ledger.post, print.
Planned (no runtime yet): Autocode.
