# cooper — the complete brief

| | |
|---|---|
| matrix layer | 208 |
| icon group | L2 |
| wave spec | `wave17/208-cooper.md` |

## Purpose

tracks goods through reuse, repair and recycling loops in the cooperative market (`net.offers`, `ledger.entries`), records in the Hoard (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ledger.read`
- `net`
- `peers`
- `camera`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | material ledger | primary | runs |
| FORTRAN | `trebago` | numeric | lifecycle model | primary | runs |
| Ada | `eve` | limits | waste limits | primary | runs |
| APL | `kron` | arrays | flow grid | primary | runs |
| MUMPS | `myra` | records | product tree | primary | runs |
| Erlang | `-` | faults | fault handling | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "next stage allowed" (an item in one state at a time: a scrapped item is never resold) in Ada as Eve + APL as KRON witnessed
- material ledger (grams per material per stage) in COBOL as Sutra + FORTRAN as Trebago witnessed
- uses per item over its expected life in FORTRAN as Trebago
- "within waste limits" in Ada as Eve
- flow grid (item by stage) in APL as KRON
- product tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id cooper
matrix 208
name Cooper
needs fs.read
needs fs.write
needs ledger.read
needs net
needs peers
needs camera
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

