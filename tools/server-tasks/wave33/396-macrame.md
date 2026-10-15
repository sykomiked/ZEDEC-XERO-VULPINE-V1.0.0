# Wave 33 task: Macrame (slot 396), Community mesh network
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 2 (029 trellis), wave 17 (214 updraft), wave 18 (221 watershed, 225 tributary), wave 19 (236 levin).
Manifest: `slot 396`, `layer L3`, `version 1.0.0`, `icon icons/l3/396-macrame.svg`, `budget 233`, model `apps/macrame_model.{h,c}`, blocks under kernel/src/apps/blocks/macrame/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Runs a community mesh network: add nodes (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), share bandwidth (`peer.serve` owner-approved, `net.command`), route around failures (`net.peers`, `sys.net`), and settle shared costs in the books (`ledger.post`, `ledger.entries`), with votes (`spaces.propose`, `spaces.vote`).
Uses: `use tributary 1` (agreeing answers); `use watershed 1` (member shares and "spend allowed"); `use levin 1` ("circuit within its breaker" for node power); `use updraft 1` (link margin); `use trellis 1` (peers per state and "may this device be revoked"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `cap_ok`: (critical) bandwidth caps (each member within their share, the backbone within its capacity) in Ada as Eve + APL as KRON witnessed
- `routes`: (critical) routing metrics (expected transmissions per link, shortest path over the link matrix) in FORTRAN as Trebago + APL as KRON witnessed
- `notice`: route change notices (who hears about a failed link) in Ada as Eve
- `links`: link matrix in APL as KRON
- `costs`: cost sharing (largest remainder, minor units, refusing on overflow) in COBOL as Sutra
- `nodes`: node tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two nodes report different routes to one peer: the route is BOTH; the test asserts state 3 and that traffic keeps flowing on the known-good link while the conflict is resolved. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Payments and network changes are owner-approved; tests stay on loopback.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, ledger.read, ledger.post, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Erlang notice`; `pending Prolog cap_ok`; `pending Logo links`.
Planned (no runtime yet): Erlang, Prolog, Logo.
