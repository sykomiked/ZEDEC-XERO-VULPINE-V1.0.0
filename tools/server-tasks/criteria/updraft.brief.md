# updraft — the complete brief

| | |
|---|---|
| matrix layer | 214 |
| icon group | L2 |
| wave spec | `wave17/214-updraft.md` |

## Purpose

runs a radio and mesh signal station: pairs peers (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), relays messages (`ehop.bands`, `ehop.seal`, `ehop.open`, `harmonic.encode`, `harmonic.decode`) and watches link quality (`net.peers`).

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | message log | primary | runs |
| FORTRAN | `trebago` | numeric | signal propagation | primary | runs |
| Ada | `eve` | limits | transmit power limits | primary | runs |
| APL | `kron` | arrays | link matrix | primary | runs |
| MUMPS | `myra` | records | relay tree | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| TRAC | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- link margin (transmit power minus path loss from an integer dB table, minus receiver floor) in FORTRAN as Trebago + APL as KRON witnessed
- "transmit allowed" (power at most the band's licensed maximum) in Ada as Eve + APL as KRON witnessed
- link matrix in APL as KRON
- relay tree and message log in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id updraft
matrix 214
name Updraft
needs net
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

