# Wave 17 task: Verger (slot 206), Congregation and club
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (043 solstice, 044 kindred), wave 6 (077 trickle), wave 7 (095 bonfire).
Manifest: `slot 206`, `layer L2`, `icon icons/l2/206-verger.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: organises a congregation or club: members and groups (`social.groups`, `social.group_create`), gatherings, giving in the books (`ledger.entries`, `ledger.balance`, `ledger.post`), and calendars in the Hoard (`files.read`, `files.write`).
Uses: `use solstice 1` (day of week, next date, overlap); `use kindred 1` (contact index, field limits, likely duplicates); `use trickle 1` (a tithe with the carried remainder, "stream allowed"); `use bonfire 1` ("may this guest join", seats left). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "may share this member's details" (the member's recorded consent covers it) in Ada as Eve + APL as KRON witnessed; giving per fund in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; attendance trend in FORTRAN as Trebago + APL as KRON witnessed; rota grid in APL as KRON; member tree in MUMPS as MYRA.
BOTH test: member 12 has one phone number in one roll and another in a second: the record is BOTH and is not shared until the person confirms.
Giving is the giver's own act; the app never moves money by itself.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, ledger.read, ledger.post, peers, notify.
Planned (no runtime yet): Python, Pascal, Swift.
