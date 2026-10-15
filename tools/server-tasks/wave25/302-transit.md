# Wave 25 task: Transit (slot 302), Parcel delivery
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (090 caravan), wave 10 (120 gridmark), wave 12 (150 pannier, 154 chainman), wave 19 (234 neap).
Manifest: `slot 302`, `layer L3`, `icon icons/l3/302-transit.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: moves parcels from sender to doorstep: pickups, routes, handoffs, proof of delivery and payment (`net.peers`, `freight.pack`, `ledger.entries`, `ledger.hold`, `ledger.release`, `files.read`, `files.write`).
Uses: `use pannier 1` ("send now", delivery receipts); `use chainman 1` (parcel grid for addresses); `use neap 1` ("entry may post"); `use caravan 1` (charged weight, "within carrier limits"); `use gridmark 1` (EAN-13 check digit, QR version). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `hours_ok` (a driver's minutes today at most the declared limit) in Ada as Eve + APL as KRON witnessed; `route` (stop order over the distance matrix) in FORTRAN as Trebago + APL as KRON witnessed; `delivery_invoice` (charged weight x rate in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `routes` grid in APL as KRON; `consignments` hierarchy in MUMPS as MYRA.
BOTH test: the courier marks parcel 410 delivered and the customer says not received: the parcel is BOTH and the payment is held in escrow (`ledger.hold`) until resolved; release is the owner's call. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Financial: holds and releases are the owner's act.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, ledger.read, ledger.post, fs.read, fs.write, notify.
Planned (no runtime yet): ML, ALGOL 60, Prolog.
Pending: `pending ML hours_ok` (proof); `pending ALGOL_60 hours_ok` (proof); `pending Prolog routes` (search).
