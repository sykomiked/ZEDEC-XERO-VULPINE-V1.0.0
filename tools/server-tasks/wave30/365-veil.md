# Wave 30 task: Veil (slot 365), Privacy shade
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (008 gatehouse), wave 12 (153 pavise), wave 14 (173 warding), wave 21 (261 rainshadow).
Manifest: `slot 365`, `layer L3`, `version 1.0.0`, `icon icons/l3/365-veil.svg`, `budget 233`, model `apps/veil_model.{h,c}`, blocks under kernel/src/apps/blocks/veil/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Hides what you choose from apps and people: shade data, set blackout periods and see who tried to look. Permissions and the perimeter (`features.get`, `sys.net`, `passkeys.list`), place sharing (`place.permit`, `place.apps`, `place.forget_here`), attempts on the record (`journal.query`). A per-app data-shading method has no method: use only existing methods; list what's missing in your report.
Uses: `use rainshadow 1` ("safe to look" pattern reused as a time window, and contact times); `use pavise 1` ("allow" and the exposure score); `use warding 1` ("key active"); `use gatehouse 1` ("is this use allowed"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `disclose_ok`: (critical) "disclose" (allowed by the person's rules AND outside every blackout window AND the asker's key active) in Ada as Eve + APL as KRON witnessed, joined with `lpres_conjoin` so BOTH is withheld
- `anonymity`: (critical) anonymity metric (group size k for a shared record from its quasi-identifiers) in FORTRAN as Trebago + APL as KRON witnessed
- `access`: access grid (apps and people x data kinds) in APL as KRON
- `fees`: data-access fees asked of outside requesters (minor units, refusing on overflow) in COBOL as Sutra
- `identity`: identity tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a request is permitted by one rule and forbidden by another: it is BOTH; the test asserts state 3 and that the data is withheld until the person decides. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Nothing is disclosed on silence; a fee never buys access the person refused.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, net, peers, location, journal, notify.
