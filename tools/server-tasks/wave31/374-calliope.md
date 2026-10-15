# Wave 31 task: Calliope (slot 374), Children's story studio
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 9 (103 cubby), wave 13 (166 limner), wave 17 (216 rill), wave 18 (220 glen, 223 spindrift).
Manifest: `slot 374`, `layer L3`, `version 1.0.0`, `icon icons/l3/374-calliope.svg`, `budget 233`, model `apps/calliope_model.{h,c}`, blocks under kernel/src/apps/blocks/calliope/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Children create illustrated stories and games with a gentle AI helper in a safe, parent-guided space: stories in the Hoard (`files.read`, `files.write`), helper suggestions from `swarm.ask` (held for the child and parent), time from `screen.check` and `screen.status`, the family's seats (`seats.list`).
Uses: `use spindrift 1` ("may publish" and the branch matrix); `use rill 1` ("age-appropriate", reading level, word help); `use limner 1` (nearest palette colour and "may this AI stroke be applied"); `use glen 1` ("this child rule takes effect"); `use cubby 1` ("may this child open this now"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `help_ok`: (critical) "show this helper suggestion" (age-appropriate AND within the child's AI-help limit AND parent allowed AI help) in Ada as Eve + APL as KRON witnessed
- `time_ok`: (critical) screen-time limits for the studio in Ada as Eve + APL as KRON witnessed
- `timing`: animation timing (frames per scene at a rate, exact) in FORTRAN as Trebago
- `steps`: story-making steps one at a time in BASIC (twin Hatchling)
- `story`: story grid in APL as KRON
- `allowance`: allowance credits (credits for finished stories, refusing on overflow) in COBOL as Sutra
- `family`: family tree in MUMPS as MYRA
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): the child and the helper want different endings: the ending is BOTH; the test asserts state 3 and that both endings are kept. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: No contact with strangers; nothing is published without the parent; allowance credits are not money.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, ai.ask, screen.time, sys.read.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Python help_ok`; `pending Pascal steps`.
Planned (no runtime yet): Python, Pascal.
