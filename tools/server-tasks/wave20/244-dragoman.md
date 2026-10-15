# Wave 20 task: Dragoman (slot 244), Language host
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (013 flint); wave 8 (100 primer); wave 10 (127 colonnade, 130 scriptorium, 126 toolchest).
Manifest: `slot 244`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/244-dragoman.svg`, `budget 233`, model `apps/dragoman_model.{h,c}`, blocks under kernel/src/apps/blocks/dragoman/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts the language layer (kernel/src/lingua with catalogue.tsv, the 104 languages; kernel/src/legacy (cobol.c, fortran.c); kernel/src/sutra; kernel/src/apps/LANGUAGE_FIT.tsv and `zxapp_lang_of` in kernel/src/apps/zxapp.c for the twin names). Runs each runtime's samples (`play.catalogue`, `play.run` on scratch state, `play.draft` untrusted), lists the daemon (`rpc.discover`, `zxvd.version`), checks for runtime updates (`update.check`), reads files (`files.read`). Installing a runtime has no method: use only existing methods; list what's missing in your report.
Uses: `use colonnade 1` (verdict across witnessed runs, fuel left, language-to-pillar index); `use flint 1` (scrollback limits and cell fit); `use primer 1` ("lesson passed"); `use scriptorium 1` (result rank); `use toolchest 1` ("kit fits" and "may install"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "this language runs" (runtime column not `-` AND its sample's `play.run` verdict TRUE) counted over the 104 in APL as KRON + FORTRAN as Trebago witnessed
- (critical) runtime limits (fuel, output at most 49152 bytes, at most 60 s) in Ada as Eve + APL as KRON witnessed
- each pillar hosts its legacy language's conformance sample: COBOL money rows in COBOL as Sutra, Fortran sums in FORTRAN as Trebago, Ada limits in Ada as Eve, APL and K arrays in APL as KRON, MUMPS records in MUMPS as MYRA
- language to AI-twin pairing (language, twin, runtime) in MUMPS as MYRA
- C++ wasm is not used: no runtime row for Dragoman
Paraconsistent (BOTH test, in test_zxapp.c): one program run in Sutra and in Trebago gives 1001 and 1000, giving BOTH; the test asserts state 3 and that both results are kept for the developer. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never install anything in tests.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: sys.read, fs.read, net, ai.ask.
Planned (no runtime yet): OCaml, Mojo, Crystal.
