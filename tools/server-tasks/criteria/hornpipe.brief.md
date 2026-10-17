# hornpipe — the complete brief

| | |
|---|---|
| matrix layer | 384 |
| icon group | L3 |
| wave spec | `wave32/384-hornpipe.md` |

## Purpose

Coaches swimming and water sports: drills and lap times in the Hoard (`files.read`, `files.write`), the clock (`sys.info`), open-water check-ins (`place.here`, `chat.post`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `location`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | club fees | primary | runs |
| FORTRAN | `trebago` | numeric | stroke analysis | primary | runs |
| Ada | `eve` | limits | cold and distance limits | primary | runs |
| APL | `kron` | arrays | lap arrays | primary | runs |
| MUMPS | `myra` | records | swimmer tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- - `swim_ok`: (critical) cold and distance limits (water temperature and minutes in it, distance from the shore at most the declared) in Ada as Eve + APL as KRON witnessed
- - `stroke`: (critical) stroke analysis (strokes per length, distance per stroke, exact) in FORTRAN as Trebago + APL as KRON witnessed
- - `laps`: lap arrays in APL as KRON
- - `fees`: club fees (minor units, refusing on overflow) in COBOL as Sutra
- - `swimmers`: swimmer tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the watch records 1:12.40 and the coach 1:11.90: the time is BOTH
- the test asserts state 3 and that both stand until reviewed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id hornpipe
matrix 384
name Hornpipe
needs fs.read
needs fs.write
needs sys.read
needs location
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

