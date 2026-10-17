# linen — the complete brief

| | |
|---|---|
| matrix layer | 363 |
| icon group | L3 |
| wave spec | `wave30/363-linen.md` |

## Purpose

Tracks crops, soil, carbon and biodiversity across a farm or forest, and earns credits for verified care: fields (`place.list`, `place.add`), records (`files.read`, `files.write`), the books (`ledger.post`, `ledger.hold`, `ledger.holds`, `ledger.release`). Satellite imagery and soil sensors have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | carbon credits | primary | runs |
| FORTRAN | `trebago` | numeric | growth models | primary | runs |
| Ada | `eve` | limits | chemical limits | primary | runs |
| APL | `kron` | arrays | field grid | primary | runs |
| MUMPS | `myra` | records | plot tree | primary | runs |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (ML): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `credit_ok`: (critical) "issue credits" (carbon gain measured on the ground AND by the second method within the declared margin AND verified by a named verifier) in Ada as Eve + APL as KRON witnessed
- - `credits`: (critical) carbon credits (tonnes CO2e x1000 to credits, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `growth`: growth models (biomass gain per year by species table) in FORTRAN as Trebago
- - `chem_ok`: chemical limits in Ada as Eve
- - `fields`: field grid in APL as KRON
- - `plots`: plot tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): satellite estimate 42 t and ground plots 31 t for one stand: the gain is BOTH
- the test (scratch books) asserts state 3 and that credits wait on `ledger.hold`, never over-issued. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id linen
matrix 363
name Linen
needs location
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

