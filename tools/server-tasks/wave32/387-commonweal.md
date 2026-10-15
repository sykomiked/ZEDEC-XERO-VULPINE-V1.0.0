# Wave 32 task: Commonweal (slot 387), Your economy
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 15 (185 skep); wave 19 (232 decanter, 234 neap); wave 21 (260 serac); wave 6 (080 ninefold).
Manifest: `slot 387`, `layer L3`, `version 1.0.0`, `icon icons/l3/387-commonweal.svg`, `budget 233`, model `apps/commonweal_model.{h,c}`, blocks under kernel/src/apps/blocks/commonweal/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Your part in the mutual-aid economy in one honest view, read only: bounties earned (`swarm.earnings`, `peer.status`, `mesh.books`), patronage grants (`grants.status`, `grants.account`), Vino routes (`swap.status`, `swap.quote`), the reserve and the dividend (`vault.status`, `vault.check`, `swap.check`), your books (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.check`).
Uses: `use serac 1` (the fee and its split, "reserve at or above the floor"); `use decanter 1` (exchange and "route allowed"); `use skep 1` (the grant ledger and "within the agent's budget"); `use neap 1` ("entry may post" and A = L + E); `use ninefold 1` (holding per form and the state-reserved form). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `advisory_ok`: (critical) advisory limits (Commonweal posts nothing; every action it suggests is shown as a step the owner takes in Coffer, Gavel or Crown) in Ada as Eve + APL as KRON witnessed
- `shares`: (critical) your share of the dividend (your weight / total weight x the pool, largest remainder, minor units) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `household`: household ledger view (grants, bounties, routes, dividend per epoch, refusing on overflow) in COBOL as Sutra
- `epochs`: epoch tables in APL as KRON
- `holdings`: holdings tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): your books and `mesh.books` disagree on last epoch's bounties: the figure is BOTH; the test (features off) asserts state 3 and that both are shown, never netted away. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Shows only; never moves money. Mutual-aid words only: a grant, an allotment, a dividend. Tests with the features off.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, swarm, peers, sys.read.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Erlang household`; `pending Prolog advisory_ok`; `pending Logo epochs`.
Planned (no runtime yet): Erlang, Prolog, Logo.
