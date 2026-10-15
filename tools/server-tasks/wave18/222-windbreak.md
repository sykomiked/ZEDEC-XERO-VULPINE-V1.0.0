# Wave 18 task: Windbreak (slot 222), The Pit Boss's desk
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (014 wake, 015 tidewatch), wave 1 (008 gatehouse), wave 3 (132 twelvefold), wave 5 (020 warren).
Manifest: `slot 222`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/222-windbreak.svg`, `budget 233`, model `apps/windbreak_model.{h,c}`, blocks under kernel/src/apps/blocks/windbreak/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts the Pit Boss and the Cage (docs/PIT_BOSS.md, docs/CAGE.md; admission in kernel/src/immigration, capabilities in kernel/src/capctl, slot, layer, budget and needs in kernel/src/apps/zxapp.c via `zxapp_slot_layer` and `zxapp_needs`, scheduling in kernel/src/sched). Shows consumers and their energy (`power.top`, `power.status`), the machine (`sys.info`), engines and switches (`features.get`), agents and budgets (`swarm.state`), events and rule actions (`log.tail`, `journal.query`). A running-program table, the Cage's cleared/held/refused queue and the rulebook have no method: use only existing methods; list what's missing in your report.
Uses: `use tidewatch 1` (`cpu_x100`, `peak` and `used_share`); `use warren 1` ("may this switch change now" and energy share per engine); `use gatehouse 1` ("is this use allowed" and apps per capability); `use wake 1` (`count` by level); `use twelvefold 1` ("conclusion follows"; evaluation stays C through `lpres_conjoin` / `lpres_disjoin`). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "this crossing is allowed" by docs/REGISTRY.md section 4 (L3 to L2 as a result for checking, L2 to L1 only as a checked input event, L1 only its own journal) in Ada as Eve + APL as KRON witnessed
- (critical) "within budget" (ticks and bytes used against the manifest's budget) in FORTRAN as Trebago + APL as KRON witnessed
- process table (program x layer, budget, capabilities) in APL as KRON
- rule-firing counts per rule as fixed records refusing on overflow in COBOL as Sutra
- quarantine records (item, desk, verdict, evidence) in MUMPS as MYRA
- an item's clearance joins its checks with `lpres_conjoin` in C (kernel order). docs/CAGE.md says "Belnap conjunction"; CLAUDE.md says kernel order, not Belnap: use `lpres_conjoin` and report the conflict
Paraconsistent (BOTH test, in test_zxapp.c): check A clears an item (TRUE) and check B refuses it (FALSE), giving BOTH; the test asserts state 3, the item stays held in the Cage, and admission is false (never admitted by default). Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Windbreak shows and proposes; a rule action runs only on the owner's approval. Never call `zxvd.shutdown` in tests.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: sys.read, journal, swarm, notify.
Planned (no runtime yet): Prolog, Perl, TeX.
