# Wave 19 task: Stratum (slot 233), Source comparison
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (063 sieve), wave 6 (042 broadsheet), wave 7 (067 lore, 069 quarry).
Manifest: `slot 233`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/233-stratum.svg`, `budget 233`, model `apps/stratum_model.{h,c}`, blocks under kernel/src/apps/blocks/stratum/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Compares sources side by side, traces citations and flags where they agree or conflict: the Hoard (`search.query`, `files.read`), the owner's Web4 feed (`web4.feed`), summaries from `swarm.ask` (held, read only with Lore's leave), accepted items (`journal.query`).
Uses: `use quarry 1` ("this claim has a source" and sources per topic); `use broadsheet 1` ("well formed"); `use lore 1` ("may the AI read this file"); `use sieve 1` ("may this be applied"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) per claim: sources supporting and contradicting (claim matrix of +1, -1, 0) counted in APL as KRON + FORTRAN as Trebago witnessed
- relevance score (hits in title, heading and body, weighted) exact in FORTRAN as Trebago
- source trust limits (trust 0..100, a minimum to count) in Ada as Eve
- citation records and the reference tree in MUMPS as MYRA
- Sutra is left out: no money or counted business records here; citations are MYRA records
Paraconsistent (BOTH test, in test_zxapp.c): source A supports a claim and source B contradicts it, giving BOTH; the test asserts state 3 and that both sides are mapped, neither picked. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: search, fs.read, net, ai.ask, journal.
Planned (no runtime yet): Common Lisp, SQL, Prolog.
