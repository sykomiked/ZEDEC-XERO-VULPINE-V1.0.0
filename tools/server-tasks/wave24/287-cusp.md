# Wave 24 task: Cusp (slot 287), Livestock
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 3 (044 kindred); wave 12 (148 poultice); wave 14 (170 sheaf); wave 19 (234 neap); wave 21 (255 glebe).
Manifest: `slot 287`, `layer L3`, `icon icons/l3/287-cusp.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: manages livestock: herd records, breeding, grazing rotation, vet visits and sales (`ledger.entries`, `ledger.post`, `net.offers`, `files.read`, `files.write`, `place.list`).
Uses: `use sheaf 1` (farm accounts, "application allowed"); `use glebe 1` (field plans); `use poultice 1` ("dose within the declared limit" for animal treatments); `use neap 1` ("entry may post"); `use kindred 1` (contact index for buyers and vets). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `sale_ok` (an animal may be sold only after every treatment's withdrawal period is over) in Ada as Eve + APL as KRON witnessed; `pasture` (grass growth per paddock per day and days of grazing left) in FORTRAN as Trebago + APL as KRON witnessed; `livestock_sales` (head x price in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `paddocks` grid in APL as KRON; `pedigree` tree in MUMPS as MYRA.
BOTH test: tag 3401 is recorded as the calf of cow 12 and of cow 19: the parentage is BOTH until a test resolves it; the pedigree is not overwritten. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Advisory; treatments are a vet's decision.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, ledger.post, fs.read, fs.write, location, net, notify.
Planned (no runtime yet): ABAP.
Pending: `pending ABAP livestock_sales` (business).
