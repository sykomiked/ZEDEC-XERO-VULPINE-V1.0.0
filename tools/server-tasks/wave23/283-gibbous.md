# Wave 23 task: Gibbous (slot 283), Body clock
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (089 wellspring), wave 12 (148 poultice), wave 14 (172 escapement), wave 16 (198 taper), wave 18 (224 tarn).
Manifest: `slot 283`, `layer L3`, `icon icons/l3/283-gibbous.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: tracks sleep, light exposure and body clock, suggesting gentle routines for better rest (advice only), from the Hoard (`files.read`, `files.write`), `screen.status` and `sys.info`. zxvd has no method for wearable and light sensor readings: use only existing methods; list what's missing in your report.
Uses: `use taper 1` ("may wake now", sleep midpoint); `use tarn 1` (breathing sessions, calm log); `use poultice 1` (care plan doses, if one is kept); `use escapement 1` (clock offset); `use wellspring 1` ("outside a safe range", weekly totals). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `routine_ok` (a suggested bedtime shift at most 30 minutes a week, no alarm before the declared wake time, screen limits kept) in Ada as Eve + APL as KRON witnessed; `circadian` (sleep midpoint and its drift over 14 nights) in FORTRAN as Trebago + APL as KRON witnessed; `plan_cost` (a wellness plan's items in minor units) in COBOL as Sutra; `night` sensor arrays in APL as KRON; `sleep_history` in MUMPS as MYRA.
BOTH test: the wearable says 5 h 10 min of sleep and the diary 7 h: the night is BOTH and shown honestly; no routine is computed from it. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Health: advice only; not a medical device.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, sys.read, screen.time, notify.
Planned (no runtime yet): Mojo.
Pending: Mojo (ai): no entry here is an obvious fit; put `pending Mojo <entry>` on the one closest to ai and name it in your report.
