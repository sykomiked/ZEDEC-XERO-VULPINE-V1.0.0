# Wave 33 task: Atelier (slot 393), No-code app studio
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 17 (213 shingle, 209 freshet); wave 20 (244 dragoman); wave 22 (265 loom); wave 10 (131 trestle).
Manifest: `slot 393`, `layer L3`, `version 1.0.0`, `icon icons/l3/393-atelier.svg`, `budget 233`, model `apps/atelier_model.{h,c}`, blocks under kernel/src/apps/blocks/atelier/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Make your own app with no code: compose existing apps and blocks (Loom), register them in their slot (Shingle), start from a template (Freshet), check every language they use (Dragoman), lay out the screens (Trestle), test in the Cage and publish to your space or the market by content id (`aipi.put`, `aipi.publish`, `web4.cid`, `spaces.set_cid`, `net.offers`), with test runs (`play.run`) and files (`files.read`, `files.write`). Cage runs and manifest checks have no method: use only existing methods; list what's missing in your report.
Uses: `use loom 1` ("this wire is valid" and the budget total); `use shingle 1` ("admit" and version ordering); `use freshet 1` ("within the template's declared limits"); `use dragoman 1` ("this language runs" and runtime limits); `use trestle 1` (row and column sizes and accessibility limits). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `publish_ok`: (critical) contract and capability checks (every wire valid AND every need known AND the slot in its layer AND the Cage cleared AND the maker confirmed) in Ada as Eve + APL as KRON + C++ wasm witnessed (three witnesses; wasm owns sandboxing here)
- `budget`: (critical) budget totals (the app's composed budget and logic-line count within ZXAPP_LOGIC_MAX 128 and ZXAPP_USES 8) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `blocks`: block grid in APL as KRON
- `parse_check`: manifest line check before publishing (counts of each directive) in APL as KRON
- `ledger`: publication ledger (publications and versions, refusing on overflow) in COBOL as Sutra
- `apps`: app tree in MUMPS as MYRA
- The C++ wasm block is built with `tools/apps/build_wasm_blocks.sh` and the .wasm committed with it.
Paraconsistent (BOTH test, in test_zxapp.c): two blocks inside a new app disagree on a wire: the app is BOTH; the test asserts state 3 and that the maker sees both before anything is published. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Nothing is published or installed without the maker's confirmation; a market listing is the owner's act.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, net, peers, swarm, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending OCaml parse_check`; `pending TeX blocks`.
Planned (no runtime yet): OCaml, TeX.
