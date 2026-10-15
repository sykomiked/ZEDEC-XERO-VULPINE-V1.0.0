# Wave 17 task: Cooper (slot 208), Reuse and recycling
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 6 (078 tallystick, 079 fairground), wave 7 (090 caravan, 093 understory), wave 9 (120 gridmark).
Manifest: `slot 208`, `layer L2`, `icon icons/l2/208-cooper.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: tracks goods through reuse, repair and recycling loops in the cooperative market (`net.offers`, `ledger.entries`), records in the Hoard (`files.read`, `files.write`).
Uses: `use tallystick 1` (invoice total); `use fairground 1` (the assurance fee, "offer allowed"); `use caravan 1` (charged weight, shipment records); `use understory 1` (rolling-window totals); `use gridmark 1` (EAN-13 check digit, QR version for an item tag). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "next stage allowed" (an item in one state at a time: a scrapped item is never resold) in Ada as Eve + APL as KRON witnessed; material ledger (grams per material per stage) in COBOL as Sutra + FORTRAN as Trebago witnessed; uses per item over its expected life in FORTRAN as Trebago; "within waste limits" in Ada as Eve; flow grid (item by stage) in APL as KRON; product tree in MUMPS as MYRA.
BOTH test: item 77 is scrapped at the yard scan and resold in a market sale: it is BOTH and counted in neither total.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, ledger.read, net, peers, camera.
Planned (no runtime yet): Erlang.
