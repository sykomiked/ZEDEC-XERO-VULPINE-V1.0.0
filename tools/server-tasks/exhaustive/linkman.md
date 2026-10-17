# linkman — the exhaustive specification

| field | value |
|---|---|
| app id | `linkman` |
| matrix layer | 200 |
| icon group | L2 |
| wave spec | `wave16/200-linkman.md` |
| status | pending |

## 1. Purpose

maps the mesh: every peer, route and name (`net.peers`, `peer.status`, `devmesh.devices`, `sys.net`, `rpc.discover`) as a live constellation you can inspect.

## 2. Capabilities (the exact registry names and scopes)

- `needs peers`
- `needs net`
- `needs sys.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use sounding 1` — call `<id>.<entry>` after the use line
- `use lodestone 1` — call `<id>.<entry>` after the use line
- `use trellis 1` — call `<id>.<entry>` after the use line
- `use signpost 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | peer register | primary | runs |
| FORTRAN | `trebago` | numeric | latency model | primary | runs |
| Ada | `eve` | limits | trust limits | primary | runs |
| APL | `kron` | arrays | adjacency matrix | primary | runs |
| MUMPS | `myra` | records | topology tree | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id linkman
matrix 200
name Linkman
needs peers
needs net
needs sys.read

```

## 6. The entry specifications (what each entry computes)

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — "trusted peer" (signed AND not revoked AND seen within the declared minutes) in Ada as Eve + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — median latency per peer in FORTRAN as Trebago + APL as KRON witnessed

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — reachability (transitive closure of the adjacency matrix) in APL as KRON

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.4 — topology tree in MUMPS as MYRA

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

