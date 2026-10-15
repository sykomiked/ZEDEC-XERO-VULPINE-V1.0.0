# Wave 18 task: Glen (slot 220), Family hub
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (142 shepherd, merged); wave 3 (043 solstice, 044 kindred); wave 6 (081 heirloom).
Manifest: `slot 220`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/220-glen.svg`, `budget 233`, model `apps/glen_model.{h,c}`, blocks under kernel/src/apps/blocks/glen/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Runs a family hub: shared calendar and contacts in the Hoard (`files.read`, `files.write`), the family's space and members (`spaces.list`, `spaces.get`), seats (`seats.list`), screen-time rules (`screen.status`, `screen.check`, `screen.set_limit` only after the guardians agree), recovery guardians (`recovery.status`).
Uses: `use solstice 1` ("two events overlap" and the next date); `use kindred 1` (contact index and likely duplicates); `use shepherd 1` (`left`, `limit_ok`, `week`); `use heirloom 1` ("release due" as k-of-n guardian approval with no waiting period). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "this child rule takes effect" = within the child limits AND approved by the guardians Heirloom's block requires, in Ada as Eve + APL as KRON witnessed
- schedule load (minutes booked per person per day) exact in FORTRAN as Trebago
- child limits (minutes 0..1440, bedtime as minutes of day) in Ada as Eve
- availability grid (family members x 15-minute slots, the common free times) in APL as KRON
- family tree (members, guardians, children, contacts) in MUMPS as MYRA
- Sutra is left out: Glen keeps no money or counted business records; household records are MYRA's
Paraconsistent (BOTH test, in test_zxapp.c): parent A sets 60 minutes and parent B 90 minutes for the same child on the same day, giving BOTH; the test asserts state 3, both shown for agreement, and the earlier rule still applies (the last edit does not win). Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Limit changes need the guardians; nothing is shared outside the family space.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, screen.time, sys.read, peers, notify.
Planned (no runtime yet): Erlang, Prolog, Java.
