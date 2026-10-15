# Wave 21 task: Plinth (slot 254), Installer
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 2 (009 lockbox), wave 4 (011 lifeboat, 012 ferry), wave 5 (019 cellar).
Manifest: `slot 254`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/254-plinth.svg`, `budget 233`, model `apps/plinth_model.{h,c}`, blocks under kernel/src/apps/blocks/plinth/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts installation (install/install_zxv.py and install/transactional_disk.py, host only today; build_system/mk_golden_disc.sh; kernel/src/blockdev; kernel/src/zxvfs; kernel/src/loader/abupdate.c for the A/B slots; kernel/src/hwkey for sealing). Room and discs (`sys.disk`, `sys.info`), the backup check (`backup.verify`), the signed image check (`update.check`). Listing block devices, partitioning, formatting and sealing a key to the machine have no method: use only existing methods; list what's missing in your report.
Uses: `use cellar 1` (`used_share` and "safe to repair"); `use ferry 1` (transfer totals and "fits on the target"); `use lockbox 1` (limits and key expiry); `use lifeboat 1` ("restore fits" and "safe to start normally"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may write this disc" = blank by both probes AND the owner typed this disc's id to confirm AND it is not the disc the system runs from, in Ada as Eve + APL as KRON witnessed
- (critical) partition sizes (ESP, system A, system B, models, encrypted data; 1 MiB aligned; the sum within the disc) in FORTRAN as Trebago + APL as KRON witnessed
- block maps (partitions x start, size) in APL as KRON
- install transaction log (steps with byte counts, refusing on overflow) in COBOL as Sutra
- partition table records in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): probe A finds no partition table (blank) and probe B finds a filesystem signature, giving BOTH; the test, on an image file, asserts state 3 and that the disc is never formatted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never touch a disc without explicit confirmation; tests use image files under build/ only. Never touch build_system/keys/.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: sys.read, fs.read, fs.write, backup, notify.
Planned (no runtime yet): Zig, Python, Simula I.
