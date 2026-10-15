# Wave 19 task: Levin (slot 236), Home wiring planner
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (021 brazier); wave 7 (094 homestead); wave 9 (109 tally); wave 10 (128 mockingbird).
Manifest: `slot 236`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/236-levin.svg`, `budget 233`, model `apps/levin_model.{h,c}`, blocks under kernel/src/apps/blocks/levin/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Plans home wiring: circuits, load calculations, test runs and safety checks, kept in the Hoard (`files.read`, `files.write`); the machine's own draw from `power.status`. Meter and clamp readings have no method: use only existing methods; list what's missing in your report.
Uses: `use homestead 1` ("command allowed" and energy totals); `use brazier 1` (temperature limits); `use mockingbird 1` (boot self-check (every stage's code 0 in order), reused for a test run's steps); `use tally 1` (rational arithmetic). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "circuit within its breaker" (load at most 80% of rating, cable rating at least the breaker, voltage drop at most the owner's limit) in Ada as Eve + APL as KRON witnessed
- (critical) load and voltage drop (current in mA from W and V; drop = 2 x length x current x resistance per metre, integer milliohms) in FORTRAN as Trebago + APL as KRON witnessed
- circuit matrix (circuits x rooms x loads) in APL as KRON
- permit records (permit fees in minor units and inspection counts, refusing on overflow) in COBOL as Sutra
- panel tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the plan puts the kitchen's 2 kW on circuit 3 while the reading shows circuit 3 idle and circuit 4 carrying it, giving BOTH; the test asserts state 3 and a miswiring flag. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory: Levin never switches a breaker or a device; a licensed electrician does the work and the inspection. Tests use figures only.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, sys.read, notify.
Planned (no runtime yet): Verilog, Simula I, Delphi.
