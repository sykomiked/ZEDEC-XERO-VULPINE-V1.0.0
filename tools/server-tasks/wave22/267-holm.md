# Wave 22 task: Holm (slot 267), Hardened offline mode
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (008 gatehouse), wave 2 (030 hedgerow), wave 3 (004 keystone), wave 5 (019 cellar).
Manifest: `slot 267`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/267-holm.svg`, `budget 233`, model `apps/holm_model.{h,c}`, blocks under kernel/src/apps/blocks/holm/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Locks a device into a hardened offline mode for kiosks or sensitive work: network off (`net.command` off), answering peers stopped (`peer.serve`), the phone link (`phone.link`), switches (`features.get`, `features.set`), simple mode (`ui.get`, `ui.set`), seats (`seats.lock`, `seats.set_auto`).
Uses: `use gatehouse 1` ("is this use allowed"); `use hedgerow 1` ("is this path reachable"); `use cellar 1` ("safe to repair"); `use keystone 1` (setting limits). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may leave hardened mode" (owner session with passphrase, not a kiosk seat) in Ada as Eve + APL as KRON witnessed
- (critical) risk score (weighted count of open surfaces) in FORTRAN as Trebago + APL as KRON witnessed
- policy matrix (profiles x switches) in APL as KRON
- change records (change numbers, refusing on overflow) in COBOL as Sutra
- config tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): profile A sets the network off and profile B sets it to lan, giving BOTH; the test asserts state 3 and that the safer value (off) applies until resolved. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Closing is at once; opening is asked first (as Hedgerow).
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: net, peers, sys.read, notify.
Planned (no runtime yet): BCPL, HyperTalk.
