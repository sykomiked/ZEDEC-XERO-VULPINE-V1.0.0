# Wave 29 task: Stereoscope (slot 350), Photo stories
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (058 pinhole), wave 12 (145 cartulary), wave 13 (166 limner, 167 tintype), wave 14 (171 mercer).
Manifest: `slot 350`, `layer L3`, `version 1.0.0`, `icon icons/l3/350-stereoscope.svg`, `budget 233`, model `apps/stereoscope_model.{h,c}`, blocks under kernel/src/apps/blocks/stereoscope/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Shoots, edits and publishes photo stories with signed originals, tracked edits and licences sold per image: photos in the Hoard (`files.read`, `files.write`, `sys.disk`), published by CID (`aipi.put`, `web4.cid`), signatures checked (`web4.verify`), licence sales in the books (`ledger.post`, `ledger.entries`).
Uses: `use tintype 1` (exposure value and "possibly altered"); `use limner 1` (nearest palette colour and "may this AI stroke be applied"); `use cartulary 1` (retention and the record hierarchy); `use mercer 1` (price from cost and margin); `use pinhole 1` ("photo fits" and device ready). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `edited_mark`: (critical) "show as edited" (the edit's content id differs from the signed original's) in Ada as Eve + APL as KRON witnessed
- `licence_sale`: (critical) licence sale (price per use kind, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `colour`: colour science (white balance gains, integer 3x3 matrices q12) in FORTRAN as Trebago
- `exposure_ok`: exposure limits (shutter, ISO within the camera's range) in Ada as Eve
- `pixels`: pixel arrays (edits as whole-row operations) in APL as KRON
- `albums`: shoot album tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): an edit and its signed original are both shown for one image: the image's state is BOTH (original TRUE, edit differs); the test asserts state 3 and that viewers see the edited mark and can open the original. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: A licence sale waits for the owner's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: camera, fs.read, fs.write, sys.read, net, ledger.read, ledger.post.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending MATLAB colour`; `pending ML edited_mark`; `pending ALGOL_60 licence_sale`.
Planned (no runtime yet): MATLAB, ML, ALGOL 60.
