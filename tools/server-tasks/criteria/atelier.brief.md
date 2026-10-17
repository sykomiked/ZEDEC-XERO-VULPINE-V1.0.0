# atelier — the complete brief

| | |
|---|---|
| matrix layer | 393 |
| icon group | L3 |
| wave spec | `wave33/393-atelier.md` |

## Purpose

Make your own app with no code: compose existing apps and blocks (Loom), register them in their slot (Shingle), start from a template (Freshet), check every language they use (Dragoman), lay out the screens (Trestle), test in the Cage and publish to your space or the market by content id (`aipi.put`, `aipi.publish`, `web4.cid`, `spaces.set_cid`, `net.offers`), with test runs (`play.run`) and files (`files.read`, `files.write`). Cage runs and manifest checks have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `net`
- `peers`
- `swarm`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | publication ledger | primary | runs |
| FORTRAN | `trebago` | numeric | budget totals | primary | runs |
| Ada | `eve` | limits | contract and capability checks | primary | runs |
| APL | `kron` | arrays | block grid | primary | runs |
| MUMPS | `myra` | records | app tree | primary | runs |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |
| WebAssembly | `wasm` | sandbox | sandboxing | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (OCaml): see docs/LANGUAGE_CATALOGUE.md for this tier
- **wasm** (WebAssembly): the host tier: the witnessed circuit; the same computation compiled to a WebAssembly module the checks can witness.

## The spec's Logic clauses (what each component computes)

- - `publish_ok`: (critical) contract and capability checks (every wire valid AND every need known AND the slot in its layer AND the Cage cleared AND the maker confirmed) in Ada as Eve + APL as KRON + C++ wasm witnessed (three witnesses
- wasm owns sandboxing here)
- - `budget`: (critical) budget totals (the app's composed budget and logic-line count within ZXAPP_LOGIC_MAX 128 and ZXAPP_USES 8) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - `blocks`: block grid in APL as KRON
- - `parse_check`: manifest line check before publishing (counts of each directive) in APL as KRON
- - `ledger`: publication ledger (publications and versions, refusing on overflow) in COBOL as Sutra
- - `apps`: app tree in MUMPS as MYRA
- - The C++ wasm block is built with `tools/apps/build_wasm_blocks.sh` and the .wasm committed with it
- Paraconsistent (BOTH test, in test_zxapp.c): two blocks inside a new app disagree on a wire: the app is BOTH
- the test asserts state 3 and that the maker sees both before anything is published. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id atelier
matrix 393
name Atelier
needs fs.read
needs fs.write
needs net
needs peers
needs swarm
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

