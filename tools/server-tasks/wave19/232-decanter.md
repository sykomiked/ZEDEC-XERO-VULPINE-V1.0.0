# Wave 19 task: Decanter (slot 232), Vino router
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (033 seedbank); wave 6 (084 forage, 073 coffer); wave 7 (091 vintner).
Manifest: `slot 232`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/232-decanter.svg`, `budget 233`, model `apps/decanter_model.{h,c}`, blocks under kernel/src/apps/blocks/decanter/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts the Vino engines (kernel/src/vino, kernel/src/vino_stores, kernel/src/airc for relay time, kernel/src/ipfs_node for storage) as a router: Vino held and quoted (`swap.status`, `swap.quote`, `ledger.balance`), held while a route runs (`ledger.hold`, `ledger.holds`, `ledger.release`), compute from another node (`peer.ask`, `peer.status`), storage (`aipi.put`, `aipi.status`), energy (`power.budget`), switches (`features.get`). Buying AIRC relay time, a storage route and a route list have no method: use only existing methods; list what's missing in your report.
Uses: `use vintner 1` (sale value (units x price, minor units)); `use forage 1` (value net of the 0.08889% fee and "claim fits"); `use coffer 1` (balance and "transfer allowed"); `use seedbank 1` (CID check and size limits). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) exchange (Vino minor units to compute ms, relay seconds or storage bytes at the route's rate num/den, remainder carried, never lost) in FORTRAN as Trebago + COBOL as Sutra witnessed
- (critical) "route allowed" (route on, its plug-in switched on by the owner (off by default), within the per-route cap and the balance) in Ada as Eve + COBOL as Sutra witnessed
- route table (routes x rate, cap, provider) in APL as KRON
- Vino ledger lines refusing on overflow in COBOL as Sutra
- holder records in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the provider reports 3600 relay seconds delivered and the holder's meter 1800, giving BOTH; the test (scratch books) asserts state 3 and that the Vino stays on `ledger.hold`, not released. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Financial: advisory and human-approved; plug-ins off by default; tests use scratch books and sandbox units only.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: ledger.read, ledger.post, peers, net, swarm, notify.
Planned (no runtime yet): Erlang, Ruby, Prolog.
