# spindrift — the complete brief

| | |
|---|---|
| matrix layer | 223 |
| icon group | L2 |
| wave spec | `wave18/223-spindrift.md` |

## Purpose

Writes, illustrates and publishes interactive stories: stories and art in the Hoard (`files.read`, `files.write`), AI-drafted passages from `swarm.ask` held for the author, published to IPFS (`aipi.put`, `web4.cid`) and recorded on the author's space (`spaces.get`, `spaces.set_cid`), printed with `print.submit`.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `net`
- `ai.ask`
- `print`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | rights records | primary | runs |
| FORTRAN | `trebago` | numeric | layout metrics | primary | runs |
| Ada | `eve` | limits | content limits | primary | runs |
| APL | `kron` | arrays | branch matrix | primary | runs |
| MUMPS | `myra` | records | chapter tree | primary | runs |
| SNOBOL | `-` | text | text | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |
| Elm | `-` | view | view and model | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (SNOBOL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "may publish" = every asset's rights cleared AND within content limits AND the author confirmed, in Ada as Eve + APL as KRON witnessed
- - layout metrics (screens per chapter, image fit totals) exact in FORTRAN as Trebago
- - content limits (passage bytes, choices per passage, age rating) in Ada as Eve
- - branch matrix (passage adjacency
- unreachable passages and dead ends found) in APL as KRON
- - rights records (asset, licence kind, fee in minor units if any) as fixed records refusing on overflow in COBOL as Sutra
- - chapter tree (chapter, passage, choice, flag) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two reachable branches set the flag "door open" TRUE and FALSE, giving BOTH
- the test asserts state 3 and that the story publishes only when the author marked that flag as a deliberate paradox, and is held for the author otherwise. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id spindrift
matrix 223
name Spindrift
needs fs.read
needs fs.write
needs net
needs ai.ask
needs print
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

