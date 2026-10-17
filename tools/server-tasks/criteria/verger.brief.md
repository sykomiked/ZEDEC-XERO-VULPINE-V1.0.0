# verger — the complete brief

| | |
|---|---|
| matrix layer | 206 |
| icon group | L2 |
| wave spec | `wave17/206-verger.md` |

## Purpose

organises a congregation or club: members and groups (`social.groups`, `social.group_create`), gatherings, giving in the books (`ledger.entries`, `ledger.balance`, `ledger.post`), and calendars in the Hoard (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | giving ledger | primary | runs |
| FORTRAN | `trebago` | numeric | attendance trends | primary | runs |
| Ada | `eve` | limits | consent limits | primary | runs |
| APL | `kron` | arrays | rota grid | primary | runs |
| MUMPS | `myra` | records | member tree | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |
| Swift | `swift` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **pascal** (Pascal): see docs/LANGUAGE_CATALOGUE.md for this tier
- **swift** (Swift): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may share this member's details" (the member's recorded consent covers it) in Ada as Eve + APL as KRON witnessed
- giving per fund in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- attendance trend in FORTRAN as Trebago + APL as KRON witnessed
- rota grid in APL as KRON
- member tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id verger
matrix 206
name Verger
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

