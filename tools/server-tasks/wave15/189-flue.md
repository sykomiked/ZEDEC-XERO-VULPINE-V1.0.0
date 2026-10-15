# Wave 15 task: Flue (slot 189), Sync
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (010 keepsake), wave 3 (033 seedbank), wave 5 (032 cove), wave 6 (087 belvedere).
Manifest: `slot 189`, `layer L2`, `icon icons/l2/189-flue.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: syncs spaces, files and backups across your own devices and your cloud top (`spaces.list`, `files.list`, `files.stat`, `files.cross`, `backup.run`, `backup.list`, `devmesh.devices`, `aipi.put`, `aipi.get`).
Uses: `use keepsake 1` (`keepsake.removed`, `keepsake.keep_ok`); `use cove 1` ("may this tunnel open", nonce discipline); `use seedbank 1` (CID check, size limits); `use belvedere 1` ("may this file go on the cloud top", page totals). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): version vector compare (dominates, equal or concurrent) in APL as KRON + FORTRAN as Trebago witnessed; "sync fits" (bytes at most the target's free bytes and quota) in Ada as Eve + APL as KRON witnessed; sync ledger (per file, per device, versions strictly rising, refused on reuse or overflow) in COBOL as Sutra; seconds to send (bytes / rate) exact in FORTRAN as Trebago; folder tree in MUMPS as MYRA.
BOTH test: a file edited offline on device A (vector [3,1]) and device B (vector [2,2]): the vectors are concurrent, the file is BOTH, and both copies are kept; there is no last-write-wins.
Never log key bytes.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, backup, peers, net.
Planned (no runtime yet): Elixir, System/360 assembler, TypeScript.
