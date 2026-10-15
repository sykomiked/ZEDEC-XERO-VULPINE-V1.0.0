# Wave 31 task: Bulwark (slot 375), Organisation key guard
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (153 pavise); wave 14 (173 warding); wave 17 (210 moraine); wave 1 (009 lockbox).
Manifest: `slot 375`, `layer L3`, `version 1.0.0`, `icon icons/l3/375-bulwark.svg`, `budget 233`, model `apps/bulwark_model.{h,c}`, blocks under kernel/src/apps/blocks/bulwark/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Protects an organisation's keys, assets and accounts with layered guardians, approvals and recovery: keys and devices (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`), guardians and recovery (`recovery.setup`, `recovery.status`, `recovery.approve`, `recovery.veto`, `recovery.claim`), transfers held (`ledger.hold`, `ledger.holds`, `ledger.release`), the record (`journal.query`, `journal.verify`).
Uses: `use pavise 1` ("allow" and the exposure score); `use warding 1` ("key active" and the key register); `use moraine 1` ("release now" and the custody register); `use lockbox 1` (strength score and key expiry). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `transfer_ok`: (critical) withdrawal limits (k of n guardians approved AND within the period's cap AND no veto) in Ada as Eve + COBOL as Sutra witnessed
- `risk`: (critical) risk score (exposure, guardian spread, expiring keys, weighted) in FORTRAN as Trebago + APL as KRON witnessed
- `access`: access matrix (people x assets) in APL as KRON
- `register`: asset register (minor units, refusing on overflow) in COBOL as Sutra
- `roles`: role hierarchy and approval layers in Ada as Eve
- `guardians`: guardian tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two guardians approve and one vetoes the same transfer: it is BOTH; the test (scratch books) asserts state 3 and the transfer frozen on `ledger.hold`. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never log key bytes; never touch build_system/keys/; every release is human-approved; tests use throwaway keys and scratch books.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, peers, ledger.read, ledger.post, journal, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Smalltalk guardians`; `pending Erlang transfer_ok`; `pending Oberon roles`.
Planned (no runtime yet): Smalltalk, Erlang, Oberon.
