# Wave 32 task: Moire (slot 380), Adaptive presentation
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 11 (137 glyphwright, 138 tint), wave 14 (169 hornbook), wave 17 (205 lampwright), wave 20 (241 shallows).
Manifest: `slot 380`, `layer L3`, `version 1.0.0`, `icon icons/l3/380-moire.svg`, `budget 233`, model `apps/moire_model.{h,c}`, blocks under kernel/src/apps/blocks/moire/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Adapts any app's look, language and input to each person: simple mode (`ui.get`, `ui.set`), languages (`xlate.languages`, `xlate.detect`, `xlate.translate`), preferences in the Hoard (`files.read`, `files.write`). Display filters and input methods have no method (as Tint and Glyphwright): use only existing methods; list what's missing in your report.
Uses: `use shallows 1` (latency limits and "show one meaning"); `use lampwright 1` (colour temperature and "within brightness limits"); `use hornbook 1` ("age-appropriate" and the next lesson step); `use glyphwright 1` (composed character and valid code point); `use tint 1` (flash guard and the filter matrix). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `readable`: (critical) readability minimums (text at least the person's size, contrast ratio at least their mark, line length within range) in Ada as Eve + APL as KRON witnessed
- `contrast`: (critical) contrast maths (relative luminance from an integer sRGB table, ratio x100) in FORTRAN as Trebago + APL as KRON witnessed
- `layout`: layout arrays (elements x size and order for the person's profile) in APL as KRON
- `parse_pref`: preference rules joined per app (bounded keys) in APL as KRON
- `licences`: licence records for fonts and voices (fixed records, refusing on overflow) in COBOL as Sutra
- `prefs`: preference tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): high contrast and colour coding conflict for one chart: the display is BOTH; the test asserts state 3 and that both needs are kept (patterns added to the high-contrast colours). Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, fs.read, fs.write, ai.ask.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Raku parse_pref`; `pending Ruby layout`; `pending Mojo contrast`.
Planned (no runtime yet): Raku, Ruby, Mojo.
