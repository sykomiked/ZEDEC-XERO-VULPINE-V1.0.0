# Wave 16 task: Topiary (slot 204), Habits
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (062 kitling), wave 6 (043 solstice, 045 bowline), wave 8 (089 wellspring), wave 10 (118 burr).
Manifest: `slot 204`, `layer L2`, `icon icons/l2/204-topiary.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: shapes habits over months with small daily tasks and reminders in the Hoard (`files.read`, `files.write`) and the companion's nudges (`companion.mood`, `swarm.ask`).
Uses: `use solstice 1` (next date of a repeating task); `use bowline 1` (next due, overdue); `use kitling 1` (growth stage); `use wellspring 1` (goal progress x100); `use burr 1` (placement of a note). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): streak length (days done in a row, a BOTH day counts as not broken) in APL as KRON + FORTRAN as Trebago witnessed; "may nudge now" (at most the daily count, not in quiet hours, rest days honoured) in Ada as Eve + APL as KRON witnessed; weekly trend exact in FORTRAN as Trebago; day grid in APL as KRON; goal tree in MUMPS as MYRA.
BOTH test: day 41 is logged done on one device and missed on another: the day is BOTH and the streak is not broken.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, notify, ai.ask.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/topiary/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): IPL, Mojo.
