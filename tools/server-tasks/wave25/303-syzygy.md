# Wave 25 task: Syzygy (slot 303), Two-party agreements
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 6 (047 sealwax), wave 13 (164 handfast), wave 14 (178 assize), wave 17 (207 fellowcraft), wave 19 (240 isthmus).
Manifest: `slot 303`, `layer L3`, `icon icons/l3/303-syzygy.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: two-party agreements witnessed in two languages: drafting, signing, obligations and disputes held fairly (`files.read`, `files.write`, `journal.query`, `web4.verify`, `pspace.post`).
Uses: `use handfast 1` ("ready to sign", obligations, payment schedule); `use fellowcraft 1` (twin runs: the clause evaluated twice and compared); `use isthmus 1` (dispute case room); `use assize 1` ("filing accepted", days left); `use sealwax 1` ("ready to sign", amount fields). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `deadline_ok` (an obligation met on or before its deadline day) in Ada as Eve + APL as KRON witnessed; `penalty` (days late x the clause's daily rate, capped at the declared maximum, minor units) in FORTRAN as Trebago + COBOL as Sutra witnessed; `contract_payments` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `clauses` matrix in APL as KRON; `parties` hierarchy in MUMPS as MYRA.
BOTH test: the two witnesses evaluate clause 6 differently: the clause is BOTH and held while the dispute is settled; the rest of the contract keeps working. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Legal and financial: advisory; signing and paying are each party's own act.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, peers, net, notify.
Planned (no runtime yet): Eiffel, Solidity, Haskell, ML.
Pending: `pending Eiffel deadline_ok` (contracts); `pending Solidity deadline_ok` (contracts); `pending Haskell deadline_ok` (proof); `pending ML deadline_ok` (proof).
