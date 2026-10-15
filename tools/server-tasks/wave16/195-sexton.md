# Wave 16 task: Sexton (slot 195), Incident response
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (014 wake, 015 tidewatch), wave 2 (027 commons), wave 4 (011 lifeboat).
Manifest: `slot 195`, `layer L2`, `icon icons/l2/195-sexton.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: responds to outages and incidents: raises alarms (`log.tail`, `journal.query`, `sys.info`), rallies people (`chat.post`, `call.start` on a person's say), and runs recovery (`backup.verify`).
Uses: `use lifeboat 1` ("safe to start normally", next repair step, "restore fits"); `use wake 1` (`wake.count`); `use tidewatch 1` (`tidewatch.cpu_x100`, `tidewatch.peak`); `use commons 1` ("may I post here"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "escalate" (no acknowledgement within the response deadline) in Ada as Eve + APL as KRON witnessed; impact (seats affected x minutes) exact in FORTRAN as Trebago; incident record (incident number, opened, acknowledged, closed, never reused) in COBOL as Sutra; alert matrix (monitor by state) in APL as KRON; on-call tree in MUMPS as MYRA.
BOTH test: monitor A says the service is up and monitor B says down: the state is BOTH and is investigated; it is never ignored.
Recovery steps are advisory; each needs a person's approval (Lifeboat's rule).
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: journal, sys.read, peers, net, notify, backup.
Planned (no runtime yet): Elixir.
