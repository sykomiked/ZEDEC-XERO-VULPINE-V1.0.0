# Wave 24 task: Rampart (slot 289), Security operations
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (018 witness), wave 12 (145 cartulary, 153 pavise), wave 14 (174 damper), wave 18 (222 windbreak).
Manifest: `slot 289`, `layer L3`, `icon icons/l3/289-rampart.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: a security operations centre for an organisation: alerts, incidents, responders and signed after-action reports (`journal.query`, `journal.verify`, `log.tail`, `power.top`, `files.write`).
Uses: `use pavise 1` (combined "allow", exposure score); `use windbreak 1` ("this crossing is allowed", quarantine records); `use damper 1` ("raise the alarm" for physical sites); `use cartulary 1` (retention days for reports); `use witness 1` (entry record, chain intact). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `playbook_ok` (a response step allowed by the incident's playbook AND approved by the responder on duty) in Ada as Eve + APL as KRON witnessed; `risk` (likelihood x impact per asset, integer) in FORTRAN as Trebago + APL as KRON witnessed; `incident_costs` (hours and items in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `correlation` grid (alert by asset by hour) in APL as KRON; `assets` hierarchy in MUMPS as MYRA.
BOTH test: tool A marks incident 77 as malware and tool B as benign: the incident is BOTH in its case file; it is neither auto-closed nor escalated wrongly. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Every response step is human-approved; reports are signed by a person.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: journal, sys.read, notify, fs.read, fs.write.
Planned (no runtime yet): RPG, ML, ALGOL 60.
Pending: `pending RPG incident_costs` (report); `pending ML playbook_ok` (proof); `pending ALGOL_60 playbook_ok` (proof).
