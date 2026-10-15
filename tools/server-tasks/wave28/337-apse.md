# Wave 28 task: Apse (slot 337), Faith community
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 4 (073 coffer), wave 9 (095 bonfire), wave 14 (172 escapement), wave 18 (223 spindrift, 224 tarn).
Manifest: `slot 337`, `layer L3`, `icon icons/l3/337-apse.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: supports faith and spiritual communities: services, readings, member care, giving and quiet reflection (`files.read`, `files.write`, `ledger.entries`, `social.groups`, `pspace.post`).
Uses: `use tarn 1` (breathing and calm sessions); `use spindrift 1` (story publishing, for readings); `use escapement 1` (shift matrix for rotas); `use bonfire 1` ("may this guest join"); `use coffer 1` (balance per account). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `privacy_ok` (member care notes shown only to those the member allowed) in Ada as Eve + APL as KRON witnessed; `calendar_calc` (movable feast dates by the declared tradition's rule, integer) in FORTRAN as Trebago + APL as KRON witnessed; `donations` (gifts per fund in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `rota` grid in APL as KRON; `congregation` tree in MUMPS as MYRA.
BOTH test: tradition A sets the feast on 14 April and tradition B on 21 April: both are honoured as BOTH; neither is forced. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Giving is the giver's own act.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, ledger.read, peers, net, notify.
Planned (no runtime yet): Bash, C#.
Pending: Bash (jobs): no entry here is an obvious fit; put `pending Bash <entry>` on the one closest to jobs and name it in your report; `pending C# congregation` (objects).
