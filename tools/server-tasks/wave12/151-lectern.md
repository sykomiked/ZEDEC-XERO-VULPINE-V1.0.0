# Wave 12 task: Lectern (slot 151), Study reader
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 5 (038 margins), wave 7 (042 broadsheet), wave 8 (069 quarry), wave 9 (106 nutshell), wave 10 (117 spyglass).
Manifest: `slot 151`, `layer L2`, `icon icons/l2/151-lectern.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: reads documents from the Hoard (`files.read`, `files.write`, `search.query`) with annotations, citations and flashcards drawn straight from what you highlight.
Uses: `use broadsheet 1` ("well formed", zoom to fit, annotation record); `use quarry 1` ("this claim has a source", citation record); `use margins 1` (note size limits, tag index); `use nutshell 1` (next box and due day for a card); `use spyglass 1` (source rectangle for zoom on a passage). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "make a card from this highlight" (offset + length inside the document, length 1..4096 bytes) in Ada as Eve + APL as KRON witnessed; citation register (citation number, source id, page) as fixed records numbered without reuse in COBOL as Sutra; reading minutes per document per week exact in FORTRAN as Trebago; source limits (at most 256 sources a document) in Ada as Eve; concept matrix (concept by source counts) in APL as KRON; chapter tree in MUMPS as MYRA.
BOTH test: source 3 marks claim 12 TRUE and source 5 marks it FALSE: the claim is BOTH and both sources are shown side by side; neither is hidden.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search.
Planned (no runtime yet): Python, Common Lisp, TRAC.
