# Wave 28 task: Atrium (slot 332), Guesthouse
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 7 (095 bonfire); wave 13 (159 skillet); wave 14 (171 mercer, 172 escapement); wave 18 (226 swale).
Manifest: `slot 332`, `layer L3`, `icon icons/l3/332-atrium.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: runs a guesthouse or venue: rooms, bookings, catering, housekeeping and guest bills (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `print.submit`).
Uses: `use skillet 1` (shopping list, "keep off the plate"); `use swale 1` (bakery orders); `use mercer 1` (stock after a sale, sale records); `use escapement 1` (shift matrix); `use bonfire 1` ("may this guest join", seats left). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `capacity_ok` (guests per room and per venue at most the declared capacity) in Ada as Eve + APL as KRON witnessed; `no_double_booking` (a room has at most one booking per night) in APL as KRON + C++ wasm witnessed; `occupancy` (next month's occupancy from bookings and history) in FORTRAN as Trebago + APL as KRON witnessed; `folios` (guest bills in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `rooms` grid in APL as KRON; `guests` history in MUMPS as MYRA.
BOTH test: room 5 is booked for 12 March by two guests: the night is BOTH and staff are alerted to rehouse one before arrival. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Allergen flags are advisory (Skillet's rule).
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, ledger.read, ledger.post, print, notify.
Planned (no runtime yet): System/360 assembler, Swift.
Pending: System/360_assembler (machine): no entry here is an obvious fit; put `pending System/360_assembler <entry>` on the one closest to machine and name it in your report; `pending Swift guests` (objects).
