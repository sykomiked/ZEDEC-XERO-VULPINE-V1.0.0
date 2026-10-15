# Wave 18 task: Tarn (slot 224), Breathing and calm
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 6 (110 sundial), wave 8 (055 birdsong, 089 wellspring), wave 11 (144 stillwater).
Manifest: `slot 224`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/224-tarn.svg`, `budget 233`, model `apps/tarn_model.{h,c}`, blocks under kernel/src/apps/blocks/tarn/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Guides breathing and focus sessions and logs calm and sleep over time (advice only): sessions and logs in the Hoard (`files.read`, `files.write`), quiet windows and notices (`notes.list`, `screen.status`), the clock from `sys.info`. Readings from a heart-rate or sleep sensor have no method: use only existing methods; list what's missing in your report.
Uses: `use stillwater 1` ("in a quiet window now"); `use wellspring 1` ("outside a safe range" and weekly totals); `use sundial 1` (local time and minutes until the next alarm); `use birdsong 1` (gain and clip for the guide sounds). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) breath pacing (inhale, hold, exhale ms per cycle; cycles per session) exact in FORTRAN as Trebago + APL as KRON witnessed
- (critical) session limits (1..60 minutes, a hold at most 7000 ms, no hold for a person who marked it unsafe) in Ada as Eve + APL as KRON witnessed
- mood grid (days x calm, sleep minutes, self-report) in APL as KRON
- session log as fixed records (sessions, minutes, refusing on overflow) in COBOL as Sutra
- wellbeing record tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a self-report of stress 2/10 (calm) and a heart rate of 118 bpm outside Wellspring's range give BOTH; the test asserts state 3 and that both are recorded and shown, with neither "all is well" nor an alarm. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advice only, never a diagnosis; readings that stay out of range point to a clinician.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, sys.read, screen.time, notify.
