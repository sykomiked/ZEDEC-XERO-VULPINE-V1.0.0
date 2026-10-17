# mummer — the complete brief

| | |
|---|---|
| matrix layer | 182 |
| icon group | L2 |
| wave spec | `wave15/182-mummer.md` |

## Purpose

produces shows: scripts, cues, music and slides in the Hoard (`files.read`, `files.write`) and the audience event as a watch hall (`pspace.create`, `hall.load`, `hall.play`, `hall.pause`, `hall.status`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | ticket ledger | primary | runs |
| FORTRAN | `trebago` | numeric | cue timing | primary | runs |
| Ada | `eve` | limits | stage safety limits | primary | runs |
| APL | `kron` | arrays | cue grid | primary | runs |
| MUMPS | `myra` | records | production tree | primary | runs |
| Groovy | `-` | scripting | scripting | primary | planned: needs Lingua front-end |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Groovy): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- cue start times (running sum of cue durations, ms) in FORTRAN as Trebago + APL as KRON witnessed
- ticket ledger (tickets per performance, seats left, amounts in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- "within stage safety limits" (audience at most the venue's capacity, level at most the cap, at most 3 flashes a second) in Ada as Eve + APL as KRON witnessed
- cue grid in APL as KRON
- production tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id mummer
matrix 182
name Mummer
needs fs.read
needs fs.write
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

