# joist — the complete brief

| | |
|---|---|
| matrix layer | 158 |
| icon group | L2 |
| wave spec | `wave13/158-joist.md` |

## Purpose

plans construction projects: structure models, materials, costs and task schedules, kept in the Hoard (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | bill of quantities | primary | runs |
| FORTRAN | `trebago` | numeric | load calculation | primary | runs |
| Ada | `eve` | limits | safety margins | primary | runs |
| APL | `kron` | arrays | material grid | primary | runs |
| MUMPS | `myra` | records | project tree | primary | runs |
| Delphi | `-` | structure | structure | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Delphi): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- bill of quantities (quantity x unit price per line, totals in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- member load (sum of the loads on it) exact in FORTRAN as Trebago + APL as KRON witnessed
- "within the safety margin" (load x 100 / rated capacity at most the declared utilisation) in Ada as Eve + APL as KRON witnessed
- material grid (material by task) in APL as KRON
- project tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id joist
matrix 158
name Joist
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

