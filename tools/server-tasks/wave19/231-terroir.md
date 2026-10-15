# Wave 19 task: Terroir (slot 231), Vineyard management
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (091 vintner), wave 2 (079 fairground), wave 8 (088 weatherglass, 093 understory).
Manifest: `slot 231`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/231-terroir.svg`, `budget 233`, model `apps/terroir_model.{h,c}`, blocks under kernel/src/apps/blocks/terroir/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Manages a vineyard: blocks and lots, harvest timing, sales in the books (`ledger.open_account`, `ledger.post`, `ledger.entries`, `ledger.balance`), the place (`place.here`), market offers (`net.offers`, `net.command` sell). There is no weather source (as Weatherglass): use only existing methods; list what's missing in your report.
Uses: `use vintner 1` (sale value, volume conservation, probe readings); `use weatherglass 1` (unit conversion and warnings); `use understory 1` (energy cost and "over budget"); `use fairground 1` (the 0.08889% fee and "offer allowed"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may spray this block" (days since last spray at least the pre-harvest interval, applications this season within the limit) in Ada as Eve + APL as KRON witnessed
- (critical) excise due (litres x strength x rate, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- ripening model (growing degree days from tenths of a degree, sugar trend) exact in FORTRAN as Trebago
- block grid (blocks x rows x readings) in APL as KRON
- vine and lot tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the lab reads sugar 240 (tenths of a degree Brix) and the field refractometer 215, giving BOTH; the test asserts state 3 and that both are shown and no harvest call is made for the grower. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Spray and excise figures are advisory; posting a sale needs the owner's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: ledger.read, ledger.post, location, peers, net, notify.
Planned (no runtime yet): FLOW-MATIC.
