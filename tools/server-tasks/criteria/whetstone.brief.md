# whetstone — the complete brief

| | |
|---|---|
| matrix layer | 125 |
| icon group | L1 |
| wave spec | `wave10/125-whetstone.md` |

## Purpose

run builds and tests and show the verdict. Runs Playground templates with `play.run` (four-valued verdict); reads files with `files.read`. zxvd has no build or test-run method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Racket | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| F# | `dotnet` | types | typed data | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JCL): see docs/LANGUAGE_CATALOGUE.md for this tier
- **dotnet** (F#): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- overall verdict over every result with `lpres_conjoin`'s kernel order (BOTH or NEITHER is not TRUE) in eve + kron witnessed
- passed, failed, held counts in kron + sutra witnessed (refused on overflow)
- durations summed exact in trebago
- retry limits (at most 3, timeout 1..3600 s) in eve
- run record (target, verdict, counts, ms) in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id whetstone
matrix 125
name Whetstone
needs sys.read
needs fs.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

