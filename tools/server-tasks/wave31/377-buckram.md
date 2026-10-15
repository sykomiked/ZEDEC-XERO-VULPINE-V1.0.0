# Wave 31 task: Buckram (slot 377), Device hardening
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (006 tideturn), wave 12 (152 ratchet, 153 pavise), wave 17 (214 updraft).
Manifest: `slot 377`, `layer L3`, `version 1.0.0`, `icon icons/l3/377-buckram.svg`, `budget 233`, model `apps/buckram_model.{h,c}`, blocks under kernel/src/apps/blocks/buckram/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Hardens devices and edge sensors: firmware checks and update rollouts (`update.check`), the fleet's links (`net.peers`, `devmesh.devices`, `devmesh.revoke`), failure reports (`log.tail`, `journal.query`), machine facts (`sys.info`). Firmware reads from other devices and tamper seals have no method: use only existing methods; list what's missing in your report.
Uses: `use ratchet 1` ("fault" and wear); `use updraft 1` (link margin and "transmit allowed"); `use pavise 1` ("allow" and the exposure score); `use tideturn 1` (version ordering and "both signatures valid"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `rollout_ok`: (critical) firmware limits (signed by both keys AND version above the device's floor AND within the staged rollout share) in Ada as Eve + APL as KRON witnessed
- `failures`: (critical) failure rate model (failures per 1000 device-days by model) in FORTRAN as Trebago + APL as KRON witnessed
- `seal`: seal check on the device (seal counter and boot code, small interpreter) in Forth (twin Cairndrake)
- `fleet`: fleet grid in APL as KRON
- `warranty`: device warranties (claims and cost in minor units, refusing on overflow) in COBOL as Sutra
- `devices`: device tree in MUMPS as MYRA
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): a device reports healthy while its seal says tampered: it is BOTH; the test asserts state 3 and that the device is isolated (revocation proposed to the owner), neither trusted nor wiped. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: No rollout or revocation without the owner; tests never write firmware; never touch build_system/keys/.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: net, peers, sys.read, journal, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending B seal`; `pending RPG warranty`.
Planned (no runtime yet): B, RPG.
