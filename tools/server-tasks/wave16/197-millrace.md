# Wave 16 task: Millrace (slot 197), Data pipelines
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (016 sounding, 022 spindle), wave 2 (071 gleaner), wave 5 (020 warren, 024 treadle).
Manifest: `slot 197`, `layer L2`, `icon icons/l2/197-millrace.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: builds data pipelines that branch, merge and route streams between apps on a schedule, from the Hoard (`files.read`, `files.write`), with the daemon's methods (`rpc.discover`), events (`log.tail`) and rates (`sys.net`). zxvd has no method for a pipeline routing method: use only existing methods; list what's missing in your report.
Uses: `use sounding 1` (`sounding.rate`); `use warren 1` ("may this switch change now", restart retries); `use treadle 1` (next run time, "within budget"); `use gleaner 1` (row filters); `use spindle 1` (`spindle.call_ok`). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "accept more" (head - tail below capacity on every stage's ring; at capacity is backpressure) in Ada as Eve + APL as KRON witnessed; flow rate per stage in FORTRAN as Trebago + APL as KRON witnessed; batch records (batch number, rows in and out, refused on overflow) in COBOL as Sutra; merge and branch over stream arrays in APL as KRON; pipeline tree in MUMPS as MYRA.
BOTH test: streams A and B carry 10 and 12 for key 4 at a merge: the value goes downstream as BOTH; nothing is dropped.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, fs.read, fs.write, journal, notify.
Planned (no runtime yet): Bourne shell, Clojure, Prolog.
