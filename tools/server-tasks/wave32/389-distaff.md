# Wave 32 task: Distaff (slot 389), Web4 site
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 2 (025 ramble), wave 12 (150 pannier), wave 13 (162 heddle), wave 17 (210 moraine), wave 18 (225 tributary).
Manifest: `slot 389`, `layer L3`, `version 1.0.0`, `icon icons/l3/389-distaff.svg`, `budget 233`, model `apps/distaff_model.{h,c}`, blocks under kernel/src/apps/blocks/distaff/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Publishes and links your own Web4 site of pages, files and services with signed links: pages in the Hoard (`files.read`, `files.write`), published by CID (`aipi.put`, `web4.cid`, `spaces.set_cid`, `pspace.create`), envelopes (`web4.open`, `web4.envelope`, `web4.verify`), keys in escrow (`recovery.status`).
Uses: `use heddle 1` ("fits the machine" (as page limits)); `use tributary 1` (agreeing answers, for mirror checks); `use pannier 1` ("send now" and delivery receipts); `use moraine 1` ("release now"); `use ramble 1` (CID check and "consent covers this"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `bandwidth_ok`: (critical) bandwidth limits (bytes served per window within the owner's cap) in Ada as Eve + APL as KRON witnessed
- `link_ok`: (critical) "link resolves" (the CID is well formed AND the target's signature verifies) in Ada as Eve + APL as KRON witnessed
- `rank`: link ranking (in-links weighted by depth) exact in FORTRAN as Trebago
- `links`: link matrix in APL as KRON
- `credits`: hosting credits (refusing on overflow) in COBOL as Sutra
- `site`: site tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two versions of a linked page exist under one name: the link is BOTH; the test asserts state 3 and that readers see the change, not a silent swap. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Nothing is published without the owner's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, net, peers, ledger.read.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Visual_Basic site`; `pending Haskell link_ok`; `pending ML bandwidth_ok`.
Planned (no runtime yet): Visual Basic, Haskell, ML.
