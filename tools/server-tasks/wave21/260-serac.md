# Wave 21 task: Serac (slot 260), Mutual-aid reserve
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness), wave 2 (083 reckoner), wave 3 (075 crown), wave 4 (073 coffer, 074 gavel).
Manifest: `slot 260`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/260-serac.svg`, `budget 233`, model `apps/serac_model.{h,c}`, blocks under kernel/src/apps/blocks/serac/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts the reserve engines (kernel/src/svault, kernel/src/surplus, kernel/src/vswap, kernel/src/pay/pay_assure.h for the fee split) in plain numbers, read only: the vault and fee buckets (`vault.status`, `vault.check`), the V-Bill dividend pool (`swap.status`, `swap.check`, `swap.quote`), snapshots (`stats.snapshot`, `stats.status`), the books (`ledger.check`, `mesh.books`), auctions and grants (`auction.status`, `grants.status`), the trail (`journal.verify`).
Uses: `use coffer 1` (balance and holdings total); `use gavel 1` (demand at each price); `use crown 1` (pool and epoch totals); `use reckoner 1` (period totals); `use witness 1` ("chain intact"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) the 0.08889% assurance fee (amount x 8889 / 10,000,000) and its split 50% reserve, 25% V-Bill dividend, 15% node bounties, 10% regenerative, summing exactly to the fee with the remainder rule of pay_assure.h, in COBOL as Sutra + FORTRAN as Trebago witnessed (copy the blocks/fee_check pattern)
- (critical) "reserve at or above the floor" (reserve x floor_den >= floor_num x (N - h) x backing, svault.h S4) in Ada as Eve + COBOL as Sutra witnessed
- epoch tables (epochs x reserve, pool, dividend) in APL as KRON
- reserve ledger lines refusing on overflow in COBOL as Sutra
- account records in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): `vault.status` and the books disagree on the reserve, giving BOTH; the test (features off) asserts state 3 and that the dividend is shown as not paid until they agree. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Shows only: Serac never moves money and never calls `swap.dividend`, `vault.deposit` or `vault.buy`. Mutual-aid words only (grant, dividend); tests with the features off.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: ledger.read, sys.read, journal.
