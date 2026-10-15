# Wave 18 task: Spindrift (slot 223), Interactive story publishing
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (033 seedbank), wave 4 (053 gesso), wave 6 (039 folio), wave 7 (056 cutwater), wave 9 (108 talewind).
Manifest: `slot 223`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/223-spindrift.svg`, `budget 233`, model `apps/spindrift_model.{h,c}`, blocks under kernel/src/apps/blocks/spindrift/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Writes, illustrates and publishes interactive stories: stories and art in the Hoard (`files.read`, `files.write`), AI-drafted passages from `swarm.ask` held for the author, published to IPFS (`aipi.put`, `web4.cid`) and recorded on the author's space (`spaces.get`, `spaces.set_cid`), printed with `print.submit`.
Uses: `use talewind 1` ("choice shown" and the story graph); `use folio 1` (page fit); `use gesso 1` (pixel operations over rows); `use seedbank 1` (CID check and size limits); `use cutwater 1` ("edit list valid" for clips in a story). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may publish" = every asset's rights cleared AND within content limits AND the author confirmed, in Ada as Eve + APL as KRON witnessed
- layout metrics (screens per chapter, image fit totals) exact in FORTRAN as Trebago
- content limits (passage bytes, choices per passage, age rating) in Ada as Eve
- branch matrix (passage adjacency; unreachable passages and dead ends found) in APL as KRON
- rights records (asset, licence kind, fee in minor units if any) as fixed records refusing on overflow in COBOL as Sutra
- chapter tree (chapter, passage, choice, flag) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two reachable branches set the flag "door open" TRUE and FALSE, giving BOTH; the test asserts state 3 and that the story publishes only when the author marked that flag as a deliberate paradox, and is held for the author otherwise. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Nothing is published without the author's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, net, ai.ask, print.
Planned (no runtime yet): SNOBOL, TeX, Elm.
