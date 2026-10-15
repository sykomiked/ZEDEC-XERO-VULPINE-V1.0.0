# Wave 15 task: Trowel (slot 184), Property upkeep
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (019 cellar), wave 3 (045 bowline), wave 6 (078 tallystick), wave 7 (094 homestead).
Manifest: `slot 184`, `layer L2`, `icon icons/l2/184-trowel.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: logs building upkeep: repairs, inspections, contractors and costs, in the Hoard (`files.list`, `files.read`, `files.write`, `sys.disk`) and the books (`ledger.entries`).
Uses: `use cellar 1` (used share, folder shares for the property's documents); `use bowline 1` (next due, overdue); `use tallystick 1` (invoice total, "paid in full"); `use homestead 1` (devices). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "safety check overdue" (days since the last gas, electrical or alarm inspection over the declared interval) in Ada as Eve + APL as KRON witnessed; repair invoices per item and per year in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; remaining life (expected life minus age, months) exact in FORTRAN as Trebago; room grid in APL as KRON; property tree in MUMPS as MYRA.
BOTH test: the contractor reports the roof sound and the owner reports a leak: the roof is BOTH until a site check.
Advisory; gas and electrical work is for qualified people.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, sys.read, ledger.read, notify.
Planned (no runtime yet): Erlang, JCL, Eiffel, Solidity.
