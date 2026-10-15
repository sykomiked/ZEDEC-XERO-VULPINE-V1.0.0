# Wave 13 task: Handfast (slot 164), Agreements
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness), wave 2 (027 commons), wave 6 (044 kindred, 047 sealwax).
Manifest: `slot 164`, `layer L2`, `icon icons/l2/164-handfast.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: drafts, negotiates and signs multi-party agreements in the Hoard (`files.read`, `files.write`) with every revision on the journal (`journal.query`, `journal.verify`) and signatures checked with `web4.verify`.
Uses: `use witness 1` (entry record, chain intact); `use commons 1` ("may I post here" for the negotiation room); `use kindred 1` (contact index, field limits); `use sealwax 1` ("ready to sign", amount fields, field record). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "ready to sign" = every clause agreed by every party (the party matrix all TRUE) in Ada as Eve + APL as KRON witnessed; obligations per party per due date in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; payment schedule (instalment n = floor(total x n / N) minus paid) in FORTRAN as Trebago + COBOL as Sutra witnessed; clause limits (at most 16 parties, clause text at most 8192 bytes) in Ada as Eve; party matrix (party by clause, four values) in APL as KRON; clause tree in MUMPS as MYRA.
BOTH test: party A marks clause 4 agreed and party B marks it disputed: clause 4 is BOTH and held in escrow, while the agreed clauses can go ahead.
Legal and financial: advisory; signing is each party's own act; the app never signs, sends or moves money.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, peers, notify.
Planned (no runtime yet): ALGOL 60, Haskell, Eiffel, Solidity.
