# Wave 31 task: Rondo (slot 370), Private cycles
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (089 wellspring), wave 12 (148 poultice), wave 14 (172 escapement, 180 apothecary), wave 16 (198 taper).
Manifest: `slot 370`, `layer L3`, `version 1.0.0`, `icon icons/l3/370-rondo.svg`, `budget 233`, model `apps/rondo_model.{h,c}`, blocks under kernel/src/apps/blocks/rondo/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Tracks personal cycles and rhythms privately, with predictions, symptoms and shared care notes, all in the Hoard (`files.read`, `files.write`); quiet hours (`screen.status`, `notes.list`); sharing only with people the person names.
Uses: `use poultice 1` (vital trends and care costs); `use escapement 1` ("job may run" for reminders); `use taper 1` ("may wake now" and sleep midpoint); `use apothecary 1` ("may sign this prescription" and billing codes); `use wellspring 1` ("outside a safe range"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `share_ok`: (critical) privacy limits (nothing leaves the device unless the person named the reader AND the item kind) in Ada as Eve + APL as KRON witnessed
- `predict`: (critical) cycle prediction (mean and spread of the last n cycle lengths in days, the next window) in FORTRAN as Trebago + APL as KRON witnessed
- `days`: day grid (days x symptoms) in APL as KRON
- `notes`: shared care notes (bounded text, kept in C) counted per reader in APL as KRON
- `billing`: clinic billing (minor units, refusing on overflow) in COBOL as Sutra
- `health`: health tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the predicted start is day 28 and the logged start day 31: the cycle is BOTH; the test asserts state 3 and that the person's log stands and the prediction shows its uncertainty. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advice only, never a diagnosis; nothing is shared without the person.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, screen.time, sys.read, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Erlang share_ok`; `pending Mojo predict`; `pending Perl notes`.
Planned (no runtime yet): Erlang, Mojo, Perl.
