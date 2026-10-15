# Wave 15 task: Lapidary (slot 191), Hardware design
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness), wave 3 (128 mockingbird), wave 7 (054 burin), wave 11 (131 trestle).
Manifest: `slot 191`, `layer L2`, `icon icons/l2/191-lapidary.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: designs schematics, boards and chip logic in the Hoard (`files.read`, `files.write`), simulates them, and publishes defensive-publication packets under the ZXV Hardware License (`aipi.put`, `web4.cid`), every revision on the journal (`journal.query`).
Uses: `use mockingbird 1` (boot self-check, memory map non-overlap, cycle count, device record); `use burin 1` (curve flattening, path record); `use trestle 1` (row and column sizes for sheet layout); `use witness 1` (entry record, chain intact). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): divider and current (Ohm's law as reduced rationals in mV and mA) in FORTRAN as Trebago + APL as KRON witnessed; "within electrical safety limits" (trace current at most its width's rating, voltage at most the clearance's) in Ada as Eve + APL as KRON witnessed; "may publish" (revision recorded AND simulation and measurement agree AND licence is the ZXV Hardware License) in Ada as Eve + APL as KRON witnessed; design revision ledger (revision numbers rising, never reused) in COBOL as Sutra; netlist arrays (every pin on exactly one net) in APL as KRON; part library in MUMPS as MYRA.
BOTH test: net 5 simulates at 3300 mV and measures 3010 mV, tolerance 50: the net is BOTH and the board is not released.
Advisory; mains-voltage designs need a qualified review.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, sys.read.
Planned (no runtime yet): VHDL, Zig, Mathematica.
