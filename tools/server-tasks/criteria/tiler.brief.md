# tiler — the complete brief

| | |
|---|---|
| matrix layer | 203 |
| icon group | L2 |
| wave spec | `wave16/203-tiler.md` |

## Purpose

assembles live dashboards from tiles drawn from the machine's data and monitors (`sys.info`, `sys.disk`, `sys.net`, `power.status`, `log.tail`), kept in the Hoard (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `journal`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | KPI records | primary | runs |
| FORTRAN | `trebago` | numeric | aggregations | primary | runs |
| Ada | `eve` | limits | refresh limits | primary | runs |
| APL | `kron` | arrays | tile grid | primary | runs |
| MUMPS | `myra` | records | dashboard tree | primary | runs |
| SAS | `-` | report | reports | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |
| Visual Basic | `dotnet` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (SAS): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.
- **dotnet** (Visual Basic): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- tile figures (sum, min, max, mean floor over the tile's series) in FORTRAN as Trebago + APL as KRON witnessed
- refresh limits (1..3600 s, at most 64 tiles) in Ada as Eve
- tile grid in APL as KRON
- dashboard tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id tiler
matrix 203
name Tiler
needs sys.read
needs journal
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

