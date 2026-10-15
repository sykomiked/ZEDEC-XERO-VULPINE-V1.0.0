# Wave 18 task: Watershed (slot 221), Cooperative projects
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (045 bowline); wave 6 (079 fairground, 073 coffer); wave 7 (096 hamlet, 086 meadow).
Manifest: `slot 221`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/221-watershed.svg`, `budget 233`, model `apps/watershed_model.{h,c}`, blocks under kernel/src/apps/blocks/watershed/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Runs cooperative projects: the pooled fund and member shares in the books (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`), escrow for disputed amounts (`ledger.hold`, `ledger.holds`, `ledger.release`), member votes (`spaces.propose`, `spaces.vote`, `spaces.get`), posts (`social.post`, `pspace.post`), market sales (`net.offers`, `net.command` sell, `mesh.books`), shared tasks in the Hoard (`files.read`, `files.write`).
Uses: `use fairground 1` (the 0.08889% fee and "offer allowed"); `use bowline 1` (next due and "overdue"); `use coffer 1` (balance per account and "transfer allowed"); `use hamlet 1` ("show this post to this seat"); `use meadow 1` ("may this seat post here"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) member shares of the pool (largest remainder, summing exactly to the pool) in COBOL as Sutra + FORTRAN as Trebago witnessed
- (critical) a project grant from the pool by approved weight, remainder carried, in FORTRAN as Trebago + COBOL as Sutra witnessed
- (critical) "spend allowed" = vote carried AND under the spending cap AND balance covers it, in Ada as Eve + COBOL as Sutra witnessed
- contribution grid (members x months) in APL as KRON
- member tree (member, role, joined, shares) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a member's record says 500 paid while `ledger.entries` shows 0 for that member, giving BOTH; the test (scratch books) asserts state 3, the 500 placed on `ledger.hold`, no change to member shares, and the co-op's other posts still allowed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Financial actions are advisory and human-approved: every post, sale and grant waits for the vote and the owner; tests use a scratch books file only. Mutual-aid words only (a grant, an allotment).
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: ledger.read, ledger.post, peers, net, fs.read, fs.write, notify.
Planned (no runtime yet): Gleam, LISP, Prolog.
