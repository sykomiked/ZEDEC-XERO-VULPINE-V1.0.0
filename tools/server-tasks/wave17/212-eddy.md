# Wave 17 task: Eddy (slot 212), Growth projections
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (109 tally), wave 4 (073 coffer), wave 7 (040 furrow), wave 8 (082 larder).
Manifest: `slot 212`, `layer L2`, `icon icons/l2/212-eddy.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: projects compounding savings, loans and long-term growth from the books (`ledger.accounts`, `ledger.balance`) with exact integer arithmetic.
Uses: `use coffer 1` (balance per account); `use larder 1` (month totals, unspent carry-over); `use furrow 1` (sums, money cells); `use tally 1` (rationals). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): balance after n periods (balance x (den + num) / den each period, floor, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed; "rate within its cap" in Ada as Eve + APL as KRON witnessed; scenario grid (rate by horizon) in APL as KRON; account records in COBOL as Sutra; household goal tree in MUMPS as MYRA.
BOTH test: rate source A gives 410 and source B 435 basis points: both are carried through the projection as BOTH, showing a range, not one falsely precise answer.
Financial: advisory, not financial advice; the app never moves money.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read.
Planned (no runtime yet): Oberon.
