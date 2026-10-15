# Wave 17 task: Updraft (slot 214), Signal station
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (016 sounding), wave 1 (029 trellis), wave 2 (027 commons), wave 5 (035 aerie).
Manifest: `slot 214`, `layer L2`, `icon icons/l2/214-updraft.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: runs a radio and mesh signal station: pairs peers (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), relays messages (`ehop.bands`, `ehop.seal`, `ehop.open`, `harmonic.encode`, `harmonic.decode`) and watches link quality (`net.peers`).
Uses: `use aerie 1` (frame tag split, "fits the band", message log); `use sounding 1` (`sounding.rate`); `use trellis 1` (peers per state, peer record); `use commons 1` ("may I post here"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): link margin (transmit power minus path loss from an integer dB table, minus receiver floor) in FORTRAN as Trebago + APL as KRON witnessed; "transmit allowed" (power at most the band's licensed maximum) in Ada as Eve + APL as KRON witnessed; link matrix in APL as KRON; relay tree and message log in MUMPS as MYRA.
BOTH test: relay 1 hears frame 31 as payload X and relay 2 as payload Y: the frame is BOTH and kept until a third relay or the checksum settles it.
Transmit only within the operator's licence; the operator keys the transmitter.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: net, peers, notify.
Planned (no runtime yet): Gleam, LabVIEW, TRAC.
