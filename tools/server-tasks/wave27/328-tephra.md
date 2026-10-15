# Wave 27 task: Tephra (slot 328), Natural hazards
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (016 sounding), wave 14 (179 cupel), wave 19 (239 isobar), wave 21 (253 fell, 259 caldera).
Manifest: `slot 328`, `layer L3`, `icon icons/l3/328-tephra.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: monitors volcanoes, earthquakes and landslides, turning sensor data into hazard maps and evacuation advice (`journal.query`, `log.tail`, `place.here`, `net.peers`, `pspace.post`). zxvd has no method for hazard sensor feeds: use only existing methods; list what's missing in your report.
Uses: `use caldera 1` (hazard watch); `use cupel 1` (integer step, precision bounds); `use isobar 1` (station readings); `use fell 1` (team room); `use sounding 1` (`sounding.rate`). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `evacuate_advice` (readiness level from the declared thresholds; an evacuation advice needs two instruments over threshold) in Ada as Eve + APL as KRON witnessed; `seismic` (event magnitude from integer amplitude tables) in FORTRAN as Trebago + APL as KRON witnessed; `response_budget` (in minor units) in COBOL as Sutra; `sensors` arrays in APL as KRON; `zones` hierarchy in MUMPS as MYRA.
BOTH test: instrument 1 shows unrest and instrument 2 is quiet: the state is BOTH, readiness rises, and no evacuation advice goes out on it. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Life-safety: advisory; official agencies order evacuations; every alert is human-approved.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: journal, location, peers, net, notify.
Planned (no runtime yet): JOVIAL, Clojure.
Pending: JOVIAL (control): no entry here is an obvious fit; put `pending JOVIAL <entry>` on the one closest to control and name it in your report; `pending Clojure sensors` (dataflow).
