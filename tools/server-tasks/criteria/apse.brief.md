# apse — the complete brief

| | |
|---|---|
| matrix layer | 337 |
| icon group | L3 |
| wave spec | `wave28/337-apse.md` |

## Purpose

supports faith and spiritual communities: services, readings, member care, giving and quiet reflection (`files.read`, `files.write`, `ledger.entries`, `social.groups`, `pspace.post`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ledger.read`
- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | donations | primary | runs |
| FORTRAN | `trebago` | numeric | calendar calculations | primary | runs |
| Ada | `eve` | limits | privacy limits | primary | runs |
| APL | `kron` | arrays | service rota grid | primary | runs |
| MUMPS | `myra` | records | congregation tree | primary | runs |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| C# | `dotnet` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bash): see docs/LANGUAGE_CATALOGUE.md for this tier
- **dotnet** (C#): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `privacy_ok` (member care notes shown only to those the member allowed) in Ada as Eve + APL as KRON witnessed
- `calendar_calc` (movable feast dates by the declared tradition's rule, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `donations` (gifts per fund in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `rota` grid in APL as KRON
- `congregation` tree in MUMPS as MYRA

## The entries and their exact checks

(no library pattern matched; author the entries from the clauses)

## The manifest contract

```
zxapp 1
id apse
matrix 337
name Apse
needs fs.read
needs fs.write
needs ledger.read
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

