# Wave 16 task: Linkman (slot 200), Mesh map
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (016 sounding), wave 1 (029 trellis), wave 5 (023 lodestone, 034 signpost).
Manifest: `slot 200`, `layer L2`, `icon icons/l2/200-linkman.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: maps the mesh: every peer, route and name (`net.peers`, `peer.status`, `devmesh.devices`, `sys.net`, `rpc.discover`) as a live constellation you can inspect.
Uses: `use sounding 1` (`sounding.rate`); `use lodestone 1` (latency stats, peer record); `use trellis 1` (peers per state, peer record); `use signpost 1` (name syntax, address class, cache record). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "trusted peer" (signed AND not revoked AND seen within the declared minutes) in Ada as Eve + APL as KRON witnessed; median latency per peer in FORTRAN as Trebago + APL as KRON witnessed; reachability (transitive closure of the adjacency matrix) in APL as KRON; topology tree in MUMPS as MYRA.
BOTH test: peer 3 reports a route to 9 through 4 and peer 5 reports 9 unreachable: the route is BOTH, shown as a split-brain network.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, sys.read.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/linkman/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Gleam, Prolog.
