# equinox — the complete brief

| | |
|---|---|
| matrix layer | 312 |
| icon group | L3 |
| wave spec | `wave26/312-equinox.md` |

## Purpose

time capsules and time-locked promises: messages, wills and payments released on a chosen date (`files.write`, `files.read`, `estate.status`, `journal.query`, `sys.info`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `sys.read`
- `ledger.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | scheduled payments | primary | runs |
| FORTRAN | `trebago` | numeric | date arithmetic | primary | runs |
| Ada | `eve` | limits | release date limits | primary | runs |
| APL | `kron` | arrays | capsule calendar grid | primary | runs |
| MUMPS | `myra` | records | recipient tree | primary | runs |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Eiffel): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `release_date_ok` (today at or after the release day AND Heirloom's release due for a will) in Ada as Eve + APL as KRON witnessed
- `days_until` (Gregorian day arithmetic) in FORTRAN as Trebago + APL as KRON witnessed
- `scheduled_payments` (amounts in minor units, released only by the owner or executor) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `calendar` grid in APL as KRON
- `recipients` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id equinox
matrix 312
name Equinox
needs fs.read
needs fs.write
needs journal
needs sys.read
needs ledger.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

