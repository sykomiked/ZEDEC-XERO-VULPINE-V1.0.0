# terroir — the complete brief

| | |
|---|---|
| matrix layer | 231 |
| icon group | L2 |
| wave spec | `wave19/231-terroir.md` |

## Purpose

Manages a vineyard: blocks and lots, harvest timing, sales in the books (`ledger.open_account`, `ledger.post`, `ledger.entries`, `ledger.balance`), the place (`place.here`), market offers (`net.offers`, `net.command` sell). There is no weather source (as Weatherglass): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `location`
- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | lot and excise records | primary | runs |
| FORTRAN | `trebago` | numeric | ripening model | primary | runs |
| Ada | `eve` | limits | spray limits | primary | runs |
| APL | `kron` | arrays | block grid | primary | runs |
| MUMPS | `myra` | records | vine and lot tree | primary | runs |
| FLOW-MATIC | `-` | business | business records | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (FLOW-MATIC): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "may spray this block" (days since last spray at least the pre-harvest interval, applications this season within the limit) in Ada as Eve + APL as KRON witnessed
- - (critical) excise due (litres x strength x rate, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - ripening model (growing degree days from tenths of a degree, sugar trend) exact in FORTRAN as Trebago
- - block grid (blocks x rows x readings) in APL as KRON
- - vine and lot tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the lab reads sugar 240 (tenths of a degree Brix) and the field refractometer 215, giving BOTH
- the test asserts state 3 and that both are shown and no harvest call is made for the grower. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id terroir
matrix 231
name Terroir
needs ledger.read
needs ledger.post
needs location
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

