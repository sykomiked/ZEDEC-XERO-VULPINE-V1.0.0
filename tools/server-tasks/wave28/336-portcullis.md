# Wave 28 task: Portcullis (slot 336), Entry control
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 2 (030 hedgerow); wave 7 (095 bonfire); wave 14 (173 warding); wave 20 (242 silt); wave 22 (269 col).
Manifest: `slot 336`, `layer L3`, `icon icons/l3/336-portcullis.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: controls entry to venues and communities: invitations, tickets, guest lists and who may come in (`web4.verify`, `net.peers`, `files.read`, `files.write`).
Uses: `use col 1` (outside gate); `use warding 1` ("key active"); `use silt 1` (records office); `use hedgerow 1` ("is this path reachable"); `use bonfire 1` ("may this guest join", seats left). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `admit` (ticket valid AND on the list AND venue under capacity) in Ada as Eve + APL as KRON witnessed; `ticket_code` (ticket code well formed, bounded bytes and check digit) in Rust wasm + APL as KRON witnessed; `crowd_flow` (people per minute through each gate) in FORTRAN as Trebago + APL as KRON witnessed; `ticket_sales` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `entry` grid in APL as KRON; `guests` hierarchy in MUMPS as MYRA.
BOTH test: ticket 5512 is valid on the box-office list and cancelled on the promoter's: it is BOTH and the guest is sent to a steward. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Capacity limits are a safety ceiling the venue sets.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: net, peers, fs.read, fs.write, notify.
Planned (no runtime yet): LISP, Zig.
Pending: `pending LISP guests` (lists); Zig (machine): no entry here is an obvious fit; put `pending Zig <entry>` on the one closest to machine and name it in your report.
