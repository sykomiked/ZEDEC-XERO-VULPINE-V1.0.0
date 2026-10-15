# Wave 21 task: Thicket (slot 262), Community update review
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness), wave 2 (006 tideturn), wave 4 (096 hamlet), wave 10 (124 graft, 129 gauntlet).
Manifest: `slot 262`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/262-thicket.svg`, `budget 233`, model `apps/thicket_model.{h,c}`, blocks under kernel/src/apps/blocks/thicket/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts community updates (kernel/src/peer_audit, kernel/src/evolve with evo_upd.c, evo_lineage.c and evo_trust.c, kernel/src/update). Signed update check (`update.check`), proposals and votes (`spaces.propose`, `spaces.vote`), the circle's discussion (`social.feed`, `social.post`), test runs (`play.run`), signatures (`web4.verify`), the trail (`journal.query`), versions (`backup.versions`). Listing forks and peer audits, submitting an audit and installing a reviewed update have no method: use only existing methods; list what's missing in your report.
Uses: `use tideturn 1` (version ordering and "both signatures valid"); `use graft 1` ("history intact" and the four-valued merge); `use gauntlet 1` (mutation score and the gate); `use witness 1` ("chain intact"); `use hamlet 1` ("show this post to this seat"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may accept" = an audit present AND the auditors agree AND tests TRUE AND both signatures valid, in Ada as Eve + APL as KRON witnessed
- (critical) test score x100 (weighted passes per suite) in FORTRAN as Trebago + APL as KRON witnessed
- review matrix (auditors x checks) in APL as KRON
- proposal ledger (proposal numbers and vote counts, refusing on overflow) in COBOL as Sutra
- fork tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): auditor A passes an update and auditor B fails it, giving BOTH; the test asserts state 3 and that it is not installed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never install anything in tests; the signed decision is the owner's.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: net, peers, sys.read, journal, backup, notify.
Now runs (Lingua front-end built): ALGOL 60 (twin Rootwyrm) for proofs of correctness: `logic algol60 <entry> apps/blocks/thicket/<entry>.alg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): SAS, ML.
