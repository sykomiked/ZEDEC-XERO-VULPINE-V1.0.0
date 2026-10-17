# ostinato — the complete brief

| | |
|---|---|
| matrix layer | 371 |
| icon group | L3 |
| wave spec | `wave31/371-ostinato.md` |

## Purpose

Runs a circular economy: goods through reuse, repair and recycling (`net.offers`, `ledger.entries`), deposits held and returned (`ledger.hold`, `ledger.holds`, `ledger.release`, `ledger.post`), records and photos in the Hoard (`files.read`, `files.write`), parcels with Caravan's records.

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `camera`
- `location`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | deposit refunds | primary | runs |
| FORTRAN | `trebago` | numeric | material flow model | primary | runs |
| Ada | `eve` | limits | hazard limits | primary | runs |
| APL | `kron` | arrays | material grid | primary | runs |
| MUMPS | `myra` | records | product lineage tree | primary | runs |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `refund_ok`: (critical) "refund the deposit" (item returned AND inspected AND not reported in use) in Ada as Eve + COBOL as Sutra witnessed
- - `deposit`: (critical) deposit refunds (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `flow`: material flow model (grams in, reused, recycled, lost
- conserved exactly) in FORTRAN as Trebago
- - `hazard_ok`: hazard limits (batteries and chemicals routed only to declared handlers) in Ada as Eve
- - `materials`: material grid in APL as KRON
- - `lineage`: product lineage tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): an item is reported returned at the depot and in use by its holder: it is BOTH
- the test (scratch books) asserts state 3 and the deposit escrowed until checked. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `conserved_sum` | truth | `whole a b c d` | trebago | `check conserved_sum whole=100,100,100,101 a=25,0,0,25 b=25,0,0,25 c=25,0,0,25 d=25,100,99,25 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id ostinato
matrix 371
name Ostinato
needs net
needs peers
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs camera
needs location
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

