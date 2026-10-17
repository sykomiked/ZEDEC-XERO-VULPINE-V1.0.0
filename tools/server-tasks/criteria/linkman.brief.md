# linkman — the complete brief

| | |
|---|---|
| matrix layer | 200 |
| icon group | L2 |
| wave spec | `wave16/200-linkman.md` |

## Purpose

maps the mesh: every peer, route and name (`net.peers`, `peer.status`, `devmesh.devices`, `sys.net`, `rpc.discover`) as a live constellation you can inspect.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `sys.read`

## The language map: which language owns which component

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

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- "trusted peer" (signed AND not revoked AND seen within the declared minutes) in Ada as Eve + APL as KRON witnessed
- median latency per peer in FORTRAN as Trebago + APL as KRON witnessed
- reachability (transitive closure of the adjacency matrix) in APL as KRON
- topology tree in MUMPS as MYRA

## The entries and their exact checks

(no library pattern matched; author the entries from the clauses)

## The manifest contract

```
zxapp 1
id linkman
matrix 200
name Linkman
needs peers
needs net
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

