# veil — the complete brief

| | |
|---|---|
| matrix layer | 365 |
| icon group | L3 |
| wave spec | `wave30/365-veil.md` |

## Purpose

Hides what you choose from apps and people: shade data, set blackout periods and see who tried to look. Permissions and the perimeter (`features.get`, `sys.net`, `passkeys.list`), place sharing (`place.permit`, `place.apps`, `place.forget_here`), attempts on the record (`journal.query`). A per-app data-shading method has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `net`
- `peers`
- `location`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | data-access fees | primary | runs |
| FORTRAN | `trebago` | numeric | anonymity metrics | primary | runs |
| Ada | `eve` | limits | disclosure limits | primary | runs |
| APL | `kron` | arrays | access grid | primary | runs |
| MUMPS | `myra` | records | identity tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- - `disclose_ok`: (critical) "disclose" (allowed by the person's rules AND outside every blackout window AND the asker's key active) in Ada as Eve + APL as KRON witnessed, joined with `lpres_conjoin` so BOTH is withheld
- - `anonymity`: (critical) anonymity metric (group size k for a shared record from its quasi-identifiers) in FORTRAN as Trebago + APL as KRON witnessed
- - `access`: access grid (apps and people x data kinds) in APL as KRON
- - `fees`: data-access fees asked of outside requesters (minor units, refusing on overflow) in COBOL as Sutra
- - `identity`: identity tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a request is permitted by one rule and forbidden by another: it is BOTH
- the test asserts state 3 and that the data is withheld until the person decides. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id veil
matrix 365
name Veil
needs sys.read
needs net
needs peers
needs location
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

