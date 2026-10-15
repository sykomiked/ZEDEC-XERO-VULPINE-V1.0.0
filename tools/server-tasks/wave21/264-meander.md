# Wave 21 task: Meander (slot 264), Handwriting to text
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (037 quillon), wave 2 (113 typecase), wave 7 (054 burin), wave 10 (114 runebook), wave 11 (137 glyphwright).
Manifest: `slot 264`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/264-meander.svg`, `budget 233`, model `apps/meander_model.{h,c}`, blocks under kernel/src/apps/blocks/meander/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Writes by hand or brush and turns strokes into text in any script: documents in the Hoard (`files.read`, `files.write`), the script's language (`xlate.detect`, `xlate.languages`). Pen input and stroke recognition have no method: use only existing methods; list what's missing in your report.
Uses: `use glyphwright 1` (composed character and valid code point); `use burin 1` (curve flattening); `use typecase 1` (script coverage); `use quillon 1` (`size_ok`); `use runebook 1` (valid code point and UTF-8 bytes). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "commit this reading" (best match's score clears the threshold and the next is at least the margin below) in Ada as Eve + APL as KRON witnessed
- (critical) strokes resampled to n points by exact integer arc length in FORTRAN as Trebago + APL as KRON witnessed
- glyph matrix (stroke features x templates, distances) in APL as KRON
- document records and the script tree in MUMPS as MYRA
- Sutra is left out: no money or counted business records here
Paraconsistent (BOTH test, in test_zxapp.c): a stroke that reads as "cl" and as "d" within the margin gives BOTH; the test asserts state 3 and that both readings stay until context or the writer settles it. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, ai.ask.
Planned (no runtime yet): Perl, Tcl, OCaml.
