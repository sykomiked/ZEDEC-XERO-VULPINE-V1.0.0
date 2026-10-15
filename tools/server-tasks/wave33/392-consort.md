# Wave 33 task: Consort (slot 392), Team matching
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 6 (044 kindred), wave 13 (164 handfast), wave 16 (200 linkman), wave 18 (221 watershed, 225 tributary).
Manifest: `slot 392`, `layer L3`, `version 1.0.0`, `icon icons/l3/392-consort.svg`, `budget 233`, model `apps/consort_model.{h,c}`, blocks under kernel/src/apps/blocks/consort/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Finds and connects people with shared skills into project teams, mentoring pairs and trusted introductions: the mesh map (`net.peers`, `peer.status`), contacts (`files.read`, `files.write`), introductions as signed envelopes held for consent (`web4.post`, `web4.consent`, `web4.verify`), the circle (`social.groups`, `social.post`).
Uses: `use linkman 1` ("trusted peer" and reachability); `use watershed 1` (member shares and the contribution grid); `use handfast 1` ("ready to sign"); `use tributary 1` (agreeing answers); `use kindred 1` (contact index and likely duplicates). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `contact_ok`: (critical) contact limits (an introduction only when both opted in AND at most n a week AND neither blocked the other) in Ada as Eve + APL as KRON witnessed
- `match`: (critical) match scoring (shared skills and availability weighted, integers) in FORTRAN as Trebago + APL as KRON witnessed
- `intro`: introduction routing (who is told what) in Ada as Eve
- `skills`: skill matrix in APL as KRON
- `contrib`: contribution ledger (refusing on overflow) in COBOL as Sutra
- `network`: network tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two references about one person conflict: it is BOTH; the test asserts state 3 and that both are shown with their sources. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: No introduction without both people's consent.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, fs.read, fs.write, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Go intro`; `pending Prolog contact_ok`; `pending Oberon network`.
Planned (no runtime yet): Go, Prolog, Oberon.
