# bindery — the complete brief

| | |
|---|---|
| matrix layer | 168 |
| icon group | L2 |
| wave spec | `wave13/168-bindery.md` |

## Purpose

catalogues a home, school or town library in the Hoard (`search.query`, `files.read`, `files.write`) and handles lending and returns, with slips printed by `print.submit`.

## Capabilities (the exact registry names for `needs`)

- `search`
- `fs.read`
- `fs.write`
- `print`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | loans ledger | primary | runs |
| FORTRAN | `trebago` | numeric | demand forecasting | primary | runs |
| Ada | `eve` | limits | loan limits | primary | runs |
| APL | `kron` | arrays | catalogue grid | primary | runs |
| MUMPS | `myra` | records | shelf hierarchy | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **pascal** (Pascal): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may lend" = a copy on the shelf AND the member under their loan limit AND no item overdue, in Ada as Eve + APL as KRON witnessed
- loans ledger (loan number, copy, member, due day
- loans per member refused on overflow) in COBOL as Sutra + APL as KRON witnessed
- demand (loans per title per month, rolling mean floor) exact in FORTRAN as Trebago
- loan limits in Ada as Eve
- catalogue grid (title by copy state) in APL as KRON
- shelf hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id bindery
matrix 168
name Bindery
needs search
needs fs.read
needs fs.write
needs print
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

