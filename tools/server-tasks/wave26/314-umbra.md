# Wave 26 task: Umbra (slot 314), Personas
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (003 threshold, 008 gatehouse), wave 13 (164 handfast), wave 14 (173 warding), wave 15 (182 mummer).
Manifest: `slot 314`, `layer L3`, `icon icons/l3/314-umbra.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: lets you act under separate personas and pseudonyms online while keeping each unlinkable and accountable (`seats.list`, `web4.agent_card`, `web4.card_verify`, `chat.me`).
Uses: `use mummer 1` (production tree, as roles); `use warding 1` ("key active" per persona); `use handfast 1` ("ready to sign" for accountability agreements); `use threshold 1` (role rights); `use gatehouse 1` ("is this use allowed"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `disclose_ok` (a field crosses from one persona to another only with the person's say for that field) in Ada as Eve + APL as KRON witnessed; `linkability` (shared attributes between two personas, weighted count) in FORTRAN as Trebago + APL as KRON witnessed; `persona_accounts` (books per persona kept apart, minor units) in COBOL as Sutra; `permissions` grid (persona by capability) in APL as KRON; `identity` tree in MUMPS as MYRA.
BOTH test: persona A and persona B are one person and present as two: the identity is BOTH, so privacy (unlinkable outside) and accountability (linked in the owner's own record) coexist. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Never used to impersonate a real person; never log key bytes.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, net, peers, notify.
Planned (no runtime yet): Smalltalk.
Pending: `pending Smalltalk identity` (objects).
