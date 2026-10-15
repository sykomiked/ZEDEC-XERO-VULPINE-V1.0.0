# Wave 14 task: Warding (slot 173), Keys for doors, devices and accounts
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (003 threshold, 008 gatehouse), wave 4 (009 lockbox), wave 9 (094 homestead).
Manifest: `slot 173`, `layer L2`, `icon icons/l2/173-warding.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: issues and revokes keys for doors, devices and accounts from one place (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`, `seats.list`). zxvd has no method for door lock control (as Homestead reports): use only existing methods; list what's missing in your report.
Uses: `use threshold 1` (role rights, seat record); `use gatehouse 1` ("is this use allowed"); `use lockbox 1` (key expiry days left, limits); `use homestead 1` ("command allowed": locks need the owner). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "key active" = issued AND not revoked on any device AND not expired, in Ada as Eve + APL as KRON witnessed; expiry limits (1..3650 days) in Ada as Eve; key register (serial numbers strictly rising, never reused, refused on overflow) in COBOL as Sutra; entropy check (bits from length x log2 of the alphabet size, integer table, inputs are lengths only) in FORTRAN as Trebago + APL as KRON witnessed; access grid (holder by door or device) in APL as KRON; holder tree in MUMPS as MYRA.
BOTH test: key 19 is revoked on device A and still active on device B: the key is BOTH and locked out everywhere until the revocation reaches every device.
Physical doors: the app never unlocks by itself; every door action needs the owner. Never log key bytes; never copy anything from build_system/keys/.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, peers, notify.
Planned (no runtime yet): Nim, Kotlin.
