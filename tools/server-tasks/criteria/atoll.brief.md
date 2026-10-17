# atoll — the complete brief

| | |
|---|---|
| matrix layer | 258 |
| icon group | L2 |
| wave spec | `wave21/258-atoll.md` |

## Purpose

Keeps a marine log: tides, catches, reef health and sightings in the Hoard (`files.read`, `files.write`, `search.query`), the place (`place.here`, `place.list`), catch sales (`ledger.post`, `ledger.entries`).

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `search`
- `ledger.read`
- `ledger.post`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | catch and quota records | primary | runs |
| FORTRAN | `trebago` | numeric | tide model | primary | runs |
| Ada | `eve` | limits | quota limits | primary | runs |
| APL | `kron` | arrays | zone grid | primary | runs |
| MUMPS | `myra` | records | species tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- - (critical) "may land this catch" (season total plus this catch within the quota, size at least the minimum, zone open) in Ada as Eve + APL as KRON witnessed
- - (critical) catch sale value (kg x price, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - tide height in mm from an integer table of constituents in FORTRAN as Trebago
- - zone grid (zones x open, closed) in APL as KRON
- - species tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two crew catch reports for one haul differ, giving BOTH
- the test asserts state 3 and that both are kept for quota review. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id atoll
matrix 258
name Atoll
needs location
needs fs.read
needs fs.write
needs search
needs ledger.read
needs ledger.post
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

