# fellowcraft — the complete brief

| | |
|---|---|
| matrix layer | 207 |
| icon group | L2 |
| wave spec | `wave17/207-fellowcraft.md` |

## Purpose

runs any job twice, in two languages (`play.run`) or on two machines (`peer.ask`, `aipi.execute`), and compares the results, each on the journal (`journal.query`).

## Capabilities (the exact registry names for `needs`)

- `ai.ask`
- `peers`
- `net`
- `journal`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | comparison ledger | primary | runs |
| FORTRAN | `trebago` | numeric | numeric diff | primary | runs |
| Ada | `eve` | limits | contract checks | primary | runs |
| APL | `kron` | arrays | four-valued truth table | primary | runs |
| MUMPS | `myra` | records | run tree | primary | runs |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Racket | `-` | parse | parsing | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bash): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- the overall verdict joins each element's result with `lpres_conjoin` (C, kernel order, never re-derived
- say so in the manifest), and the counts per value are in APL as KRON
- elements that differ and the largest difference exact in FORTRAN as Trebago + APL as KRON witnessed
- contract checks (both runs inside their fuel and output limits) in Ada as Eve
- comparison ledger (comparisons, agreements and disagreements, refused on overflow) in COBOL as Sutra
- run tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id fellowcraft
matrix 207
name Fellowcraft
needs ai.ask
needs peers
needs net
needs journal
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

