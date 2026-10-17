# chintz — the complete brief

| | |
|---|---|
| matrix layer | 346 |
| icon group | L3 |
| wave spec | `wave29/346-chintz.md` |

## Purpose

Identifies, catalogues and monitors plants in the field or greenhouse: photos and labels in the Hoard (`files.read`, `files.write`, `search.query`), the place after `place.ask`, labels printed (`print.submit`), identification suggestions from `swarm.ask` (held).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `location`
- `camera`
- `print`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | herbarium loan records | primary | runs |
| FORTRAN | `trebago` | numeric | growth curves | primary | runs |
| Ada | `eve` | limits | pesticide limits | primary | runs |
| APL | `kron` | arrays | trait matrix | primary | runs |
| MUMPS | `myra` | records | taxonomy tree | primary | runs |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Forth | `forth` | control | machine control | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Julia): see docs/LANGUAGE_CATALOGUE.md for this tier
- **forth** (Forth): the run tier: stack words. '<entry> ( args -- result )' with integer words. Range laws are natural here.

## The spec's Logic clauses (what each component computes)

- - `spray_ok`: (critical) pesticide limits (rate at most the label maximum, days since the last spray at least the interval, never on a protected species) in Ada as Eve + APL as KRON witnessed
- - `growth`: (critical) growth curve per plant (height mm by date, rate per week) in FORTRAN as Trebago + APL as KRON witnessed
- - `probe`: greenhouse probe raw counts to tenths of a degree and moisture, with a range check, in Forth (twin Cairndrake)
- - `traits`: trait matrix (specimens x traits) in APL as KRON
- - `loans`: herbarium loan records (loans out, refusing on overflow) in COBOL as Sutra
- - `taxa`: taxonomy tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): two experts give different names for one specimen: the identification is BOTH
- the test asserts state 3 and that both names and their evidence stay on the label until they agree. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `within_range` | truth | `value min max` | eve | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id chintz
matrix 346
name Chintz
needs fs.read
needs fs.write
needs search
needs location
needs camera
needs print
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

