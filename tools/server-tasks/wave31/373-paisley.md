# Wave 31 task: Paisley (slot 373), Symmetric patterns
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 7 (054 burin), wave 13 (162 heddle, 166 limner), wave 16 (203 tiler), wave 18 (227 cirque).
Manifest: `slot 373`, `layer L3`, `version 1.0.0`, `icon icons/l3/373-paisley.svg`, `budget 233`, model `apps/paisley_model.{h,c}`, blocks under kernel/src/apps/blocks/paisley/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Designs symmetric patterns, textiles, tiles and logos with live radial symmetry, then exports to print or loom: designs in the Hoard (`files.read`, `files.write`), printed (`print.status`, `print.submit`, `print.job`), print orders in the books (`ledger.post`).
Uses: `use tiler 1` (tile figures); `use limner 1` (nearest palette colour and asset licences); `use heddle 1` (drawdown, yarn length and "fits the machine"); `use cirque 1` ("dimensions close"); `use burin 1` (curve flattening and hatch lines). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `print_ok`: (critical) printer limits (size, colours and resolution within the printer's or loom's) in Ada as Eve + APL as KRON witnessed
- `symmetry`: (critical) radial symmetry (rotate each motif point by 360/n using an integer sine table, mirrored when set) in FORTRAN as Trebago + APL as KRON witnessed
- `toolpath`: cut and stitch path order over the motif (shortest pen-up travel) in APL as KRON
- `patterns`: pattern arrays in APL as KRON
- `orders`: print orders (minor units, refusing on overflow) in COBOL as Sutra
- `versions`: design version tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two collaborators edit the same motif differently: it is BOTH; the test asserts state 3 and that both are kept as variants. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: An order waits for the owner's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, print, ledger.read, ledger.post.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending APT toolpath`; `pending Logo symmetry`; `pending SNOBOL versions`.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/paisley/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): APT, SNOBOL.
