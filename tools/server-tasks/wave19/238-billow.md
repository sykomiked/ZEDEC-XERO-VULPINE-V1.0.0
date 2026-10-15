# Wave 19 task: Billow (slot 238), Live audio shows
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (033 seedbank), wave 7 (052 fireglow), wave 8 (055 birdsong), wave 11 (135 ticker).
Manifest: `slot 238`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/238-billow.svg`, `budget 233`, model `apps/billow_model.{h,c}`, blocks under kernel/src/apps/blocks/billow/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Broadcasts live audio shows to the mesh with captions and archives: the show plan and segments (`stream.plan`, `stream.segment`), a listening hall (`hall.load`, `hall.play`, `hall.pause`, `hall.status`), the show's space (`pspace.create`, `pspace.post`), captions translated (`xlate.translate`), the archive (`files.write`, `aipi.put`, `web4.cid`). Live audio capture has no method: use only existing methods; list what's missing in your report.
Uses: `use fireglow 1` (time left at a speed and chapter from position); `use birdsong 1` (gain and clip, clip length); `use seedbank 1` (CID check); `use ticker 1` ("caption stays long enough"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) loudness limits (integrated level target and true-peak cap in tenths of a dB) in Ada as Eve + APL as KRON witnessed
- audio levels (mean square over a block in integers) in FORTRAN as Trebago
- schedule grid (days x slots, no two shows overlap) in APL as KRON
- airtime and rights log (airtime minutes per show, rights fees in minor units, refusing on overflow) in COBOL as Sutra
- episode tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the caption says "fifteen" and the aligned transcript "fifty", giving BOTH; the test asserts state 3 and that the archived transcript is held for correction, not published. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: mic, peers, net, fs.read, fs.write, ai.ask.
Planned (no runtime yet): Go, Python, AWK.
