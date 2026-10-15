# Wave 31 task: Brocade (slot 376), Fashion collections
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 10 (116 dyer), wave 13 (162 heddle), wave 14 (171 mercer), wave 15 (182 mummer, 183 bodkin).
Manifest: `slot 376`, `layer L3`, `version 1.0.0`, `icon icons/l3/376-brocade.svg`, `budget 233`, model `apps/brocade_model.{h,c}`, blocks under kernel/src/apps/blocks/brocade/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Designs, shows and sells fashion collections: patterns and lookbooks in the Hoard (`files.read`, `files.write`), shows as a hall (`pspace.create`, `hall.load`, `hall.play`, `hall.status`), made-to-order sales in the books (`ledger.post`, `ledger.entries`), cut sheets printed (`print.submit`).
Uses: `use bodkin 1` (piece size, "measurements plausible", the order book); `use heddle 1` (drawdown and yarn length); `use mummer 1` (cue start times and "within stage safety limits"); `use mercer 1` (price from cost and margin); `use dyer 1` (RGB to HSL and contrast ratio). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `size_ok`: (critical) size limits (each measurement within the size chart's range, ease within the style's) in Ada as Eve + APL as KRON witnessed
- `fabric`: (critical) fabric yield (pieces laid on the width, metres per garment, waste exact) in FORTRAN as Trebago + APL as KRON witnessed
- `orders`: order ledger (minor units, refusing on overflow) in COBOL as Sutra
- `colourways`: colourway grid in APL as KRON
- `labels`: care and size label text checks (bounded fields) in APL as KRON
- `collection`: collection tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a fitting note says the waist is too tight while the measurement is within ease: the fit is BOTH; the test asserts state 3 and that the tailor is asked before cutting. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: A sale waits for the owner's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, print, peers, net, ledger.read, ledger.post.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending TRAC labels`; `pending Icon size_ok`; `pending TeX orders`.
Planned (no runtime yet): TRAC, Icon, TeX.
