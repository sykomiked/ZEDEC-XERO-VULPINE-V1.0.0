# Wave 33 task: Seachest (slot 403), Sealed custody
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (153 pavise, 145 cartulary); wave 14 (173 warding); wave 17 (210 moraine); wave 6 (081 heirloom).
Manifest: `slot 403`, `layer L3`, `version 1.0.0`, `icon icons/l3/403-seachest.svg`, `budget 233`, model `apps/seachest_model.{h,c}`, blocks under kernel/src/apps/blocks/seachest/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Keeps wills, deeds, keys and valuables in sealed custody with timed releases and guardian approvals: the estate (`estate.status`, `estate.manifest`), guardians (`recovery.status`, `recovery.approve`, `recovery.veto`, `recovery.claim`), keys (`passkeys.list`), records in the Hoard (`files.read`, `files.write`), the record (`journal.query`, `journal.verify`).
Uses: `use warding 1` ("key active"); `use pavise 1` ("allow"); `use cartulary 1` (retention and the record hierarchy); `use moraine 1` ("release now" and the custody register); `use heirloom 1` ("release due" and manifest fractions). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `access_ok`: (critical) access limits (a release only when Heirloom's release due AND the guardians approve AND no claim is open) in Ada as Eve + COBOL as Sutra witnessed
- `schedule`: (critical) release scheduling (dates from the will's terms, days since the trigger) in FORTRAN as Trebago + APL as KRON witnessed
- `custody`: custody ledger (items in and out, refusing on overflow) in COBOL as Sutra
- `items`: item grid in APL as KRON
- `beneficiaries`: beneficiary tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two claims to one deed: it is BOTH; the test asserts state 3 and that nothing is released until resolved. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Nothing happens on time or silence alone; owner and guardians sign. Tests make their own throwaway keys; never touch build_system/keys/.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, journal, fs.read, fs.write, sys.read, ledger.read, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Eiffel access_ok`; `pending Solidity custody`.
Planned (no runtime yet): Eiffel, Solidity.
