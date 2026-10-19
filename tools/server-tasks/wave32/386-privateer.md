# Wave 32 task: Privateer (slot 386), Threat hunting
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (153 pavise); wave 14 (174 damper); wave 15 (181 loupe); wave 18 (225 tributary); wave 2 (030 hedgerow).
Manifest: `slot 386`, `layer L3`, `version 1.0.0`, `icon icons/l3/386-privateer.svg`, `budget 233`, model `apps/privateer_model.{h,c}`, blocks under kernel/src/apps/blocks/privateer/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Hunts threats on the network and devices, tests defences safely and contains intrusions: events (`log.tail`, `journal.query`, `journal.verify`), files (`files.stat`, `search.query`), network mode (`net.command` off/lan, owner-approved), peers (`net.peers`, `devmesh.devices`, `devmesh.revoke` owner-approved), switches (`features.get`).
Uses: `use loupe 1` ("evidence admissible" and timeline gaps); `use pavise 1` ("allow" and the exposure score); `use damper 1` ("raise the alarm"); `use tributary 1` (agreeing answers); `use hedgerow 1` ("is this path reachable" and address class). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `contain_ok`: (critical) containment limits (isolate only the named device or path; never the owner's last admin device) in Ada as Eve + APL as KRON witnessed
- `anomaly`: (critical) anomaly score (events per minute against the baseline by kind, integer z-like score) in FORTRAN as Trebago + APL as KRON witnessed
- `alerts`: alert routing (who hears which severity) in Ada as Eve
- `events`: event matrix in APL as KRON
- `costs`: incident costs (minor units, refusing on overflow) in COBOL as Sutra
- `assets`: asset tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): one detector calls an event benign and another malicious: it is BOTH; the test asserts state 3 and that the item is sandboxed (held), neither ignored nor blocked outright. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Containment is proposed and the owner approves; defence tests run only on the owner's own machines; never go online in tests.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: journal, search, fs.read, sys.read, net, peers, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Go alerts`; `pending BCPL contain_ok`.
Planned (no runtime yet): Go, BCPL.
