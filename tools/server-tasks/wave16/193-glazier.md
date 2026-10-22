# Wave 16 task: Glazier (slot 193), Open public records
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness, 033 seedbank), wave 2 (083 reckoner), wave 8 (087 belvedere).
Manifest: `slot 193`, `layer L2`, `icon icons/l2/193-glazier.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: publishes budgets and decisions from the books (`ledger.accounts`, `ledger.entries`, `ledger.check`) openly (`aipi.put`, `web4.cid`, `spaces.set_cid`, `files.write`) so anyone can verify them, with the journal checked by `journal.verify`.
Uses: `use witness 1` (entry record, chain intact); `use seedbank 1` (CID check, size limits, pin record); `use reckoner 1` (period totals, "report allowed"); `use belvedere 1` ("may this file go on the cloud top"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): public accounts per line in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; month-on-month change per line in FORTRAN as Trebago + APL as KRON witnessed; "may publish" (marked public AND no personal fields AND approved by an owner) in Ada as Eve + APL as KRON witnessed; budget grid in APL as KRON; agency tree in MUMPS as MYRA.
BOTH test: line 12 is 1000000 in the ledger and 1050000 in the approved report: both figures are published as BOTH, showing the gap.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, fs.read, fs.write, net, peers, journal.
Now runs (Lingua front-end built): ALGOL 60 (twin Rootwyrm) for proofs of correctness: `logic algol60 <entry> apps/blocks/glazier/<entry>.alg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Haskell, TeX.
