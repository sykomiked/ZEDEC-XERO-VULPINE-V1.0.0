# Wave 22 task: Chrysalis (slot 268), Guest machines
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (015 tidewatch), wave 1 (008 gatehouse), wave 3 (128 mockingbird), wave 5 (019 cellar), wave 6 (020 warren).
Manifest: `slot 268`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/268-chrysalis.svg`, `budget 233`, model `apps/chrysalis_model.{h,c}`, blocks under kernel/src/apps/blocks/chrysalis/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts guest machines (kernel/src/hwemu with hwe.c and hwa.c, kernel/src/virtio, kernel/src/legacy, kernel/src/capctl; no hypervisor exists yet). The host (`sys.info`, `sys.disk`), energy and budgets (`power.top`, `power.status`, `power.budget`), switches (`features.get`), guest disk images in the Hoard (`files.read`, `files.write`). Creating, starting and pausing a guest, and the driver machine, have no method: use only existing methods; list what's missing in your report.
Uses: `use mockingbird 1` (memory map regions do not overlap, boot self-check, limits); `use warren 1` ("may this switch change now"); `use gatehouse 1` ("is this use allowed"); `use tidewatch 1` (`cpu_x100` and `peak`); `use cellar 1` (`used_share`). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) CPU and memory shares (guests' shares plus the host reserve within the machine) in FORTRAN as Trebago + APL as KRON witnessed
- (critical) "guest isolated" (no shared memory beyond its declared rings, no device passed without the owner) in Ada as Eve + APL as KRON + C++ wasm (three witnesses; wasm owns sandboxing here)
- guest table (guests x CPU, memory, devices) in APL as KRON
- guest ledger (CPU ms counters, refusing on overflow) in COBOL as Sutra
- guest disk image records in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a guest reports itself idle while the host measures it at 100% CPU, giving BOTH; the test asserts state 3 and that the guest is paused, not trusted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Tests never start a guest; image files only.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: sys.read, fs.read, fs.write, notify.
Planned (no runtime yet): Crystal.
