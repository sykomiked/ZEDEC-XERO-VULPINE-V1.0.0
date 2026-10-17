# escutcheon — the complete brief

| | |
|---|---|
| matrix layer | 315 |
| icon group | L3 |
| wave spec | `wave26/315-escutcheon.md` |

## Purpose

insurance for households and co-ops: policies, premiums, claims, evidence and mutual-aid grants (`ledger.accounts`, `ledger.entries`, `ledger.post`, `ledger.hold`, `files.read`, `files.write`, `journal.query`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | premiums and mutual-aid grants | primary | runs |
| FORTRAN | `trebago` | numeric | risk model | primary | runs |
| Ada | `eve` | limits | cover limits | primary | runs |
| APL | `kron` | arrays | claim grid | primary | runs |
| MUMPS | `myra` | records | policy holder tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- `cover_ok` (a claim inside the policy's cover, limits and dates) in Ada as Eve + COBOL as Sutra witnessed
- `risk` (expected claims per pool from history, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `premiums_grants` (premiums in, mutual-aid grants out, minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `claims` grid in APL as KRON
- `holders` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id escutcheon
matrix 315
name Escutcheon
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

