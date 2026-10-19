# Wave 23 task: Aureole (slot 277), Council
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (018 witness); wave 3 (043 solstice); wave 14 (178 assize); wave 20 (242 silt); wave 22 (271 cairn).
Manifest: `slot 277`, `layer L3`, `icon icons/l3/277-aureole.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: runs a town or co-op council: agendas, motions, minutes, budgets and signed decisions in one public record (`spaces.propose`, `spaces.vote`, `spaces.get`, `journal.query`, `web4.verify`, `files.write`).
Uses: `use cairn 1` (proposals and votes); `use assize 1` ("filing accepted", days left); `use silt 1` (records office stamping and retention); `use witness 1` (entry record, chain intact); `use solstice 1` (day of week, "two events overlap"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `motion_passes` (quorum present AND votes for over the declared share of votes cast) in Ada as Eve + APL as KRON witnessed; `vote_tally` (counts per option over the ballot vector) in APL as KRON + FORTRAN as Trebago witnessed; `budget_lines` (municipal budget per line in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed; `revenue` (next year's revenue from the trend) in FORTRAN as Trebago; `bylaws` hierarchy in MUMPS as MYRA.
BOTH test: bylaw 12 permits market stalls on Sundays and bylaw 30 forbids them: the rule is BOTH and held in escrow for the council; enforcement does not crash or pick one. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Decisions are the council's; the app records and counts, it never decides.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, journal, fs.write, notify.
Planned (no runtime yet): PHP, Haskell, ML.
Pending: `pending PHP budget_lines` (report); `pending Haskell motion_passes` (proof); `pending ML motion_passes` (proof).
