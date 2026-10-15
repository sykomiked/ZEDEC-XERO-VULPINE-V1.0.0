# Wave 16 task: Stationer (slot 202), Forms and templates
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (115 platen), wave 6 (039 folio, 047 sealwax), wave 7 (054 burin).
Manifest: `slot 202`, `layer L2`, `icon icons/l2/202-stationer.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: designs forms, letters and printed templates in the Hoard (`files.read`, `files.write`), prints (`print.status`, `print.submit`) and scans them (`scan.start`), and folds them into fill-and-sign packets.
Uses: `use folio 1` (page fit, amount tables, page limits); `use sealwax 1` ("ready to sign", amount fields, fill order); `use burin 1` (paths); `use platen 1` (`platen.sheets`, `platen.copies_ok`). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): field boxes fit the page (twips) in FORTRAN as Trebago + APL as KRON witnessed; "field valid" (length, range and required as declared) in Ada as Eve + APL as KRON witnessed; form registry (form number, version, never reused) in COBOL as Sutra; field grid in APL as KRON; form tree in MUMPS as MYRA.
BOTH test: field 7 reads 1200 on copy A and 1250 on copy B: the field is BOTH and the packet is not ready to sign until the signer confirms.
Signing is the person's own act; the app never signs or submits.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, print, scan.
Planned (no runtime yet): PostScript, APT, Lua.
