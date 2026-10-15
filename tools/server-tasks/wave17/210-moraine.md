# Wave 17 task: Moraine (slot 210), Key escrow
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (009 lockbox, 008 gatehouse, 018 witness), wave 6 (081 heirloom).
Manifest: `slot 210`, `layer L2`, `icon icons/l2/210-moraine.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: holds keys in escrow with guardians (`recovery.setup`, `recovery.status`, `recovery.approve`, `recovery.veto`, `recovery.claim`, `recovery.finalize`, `recovery.cancel`), grants time-limited access, and logs every release (`journal.query`).
Uses: `use lockbox 1` (key limits, expiry); `use gatehouse 1` ("is this use allowed"); `use heirloom 1` ("release due": k approvals, waiting period over, no living veto); `use witness 1` (entry record, chain intact). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "release now" (Heirloom's release due AND access window not over AND requester allowed) in Ada as Eve + APL as KRON witnessed; custody register (release number, guardian, time, never reused) in COBOL as Sutra; key-strength score (from lengths and counts only) in FORTRAN as Trebago + APL as KRON witnessed; access grid in APL as KRON; guardian hierarchy in MUMPS as MYRA.
BOTH test: guardian 2 approves and guardian 4 vetoes within the waiting period: the release is BOTH and held; the key is neither released nor lost until the rule resolves it.
Never log or store key bytes; tests use lengths and counts only. Never copy anything from build_system/keys/.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, journal, notify, sys.read.
Planned (no runtime yet): Eiffel, Solidity.
