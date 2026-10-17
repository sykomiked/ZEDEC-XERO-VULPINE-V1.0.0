# cupola — the complete brief

| | |
|---|---|
| matrix layer | 282 |
| icon group | L3 |
| wave spec | `wave23/282-cupola.md` |

## Purpose

preserves records for decades: checks integrity, migrates old formats and keeps copies on disk, on peers and as content-addressed blocks (`backup.verify`, `backup.versions`, `aipi.put`, `aipi.get`, `web4.cid`, `files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `backup`
- `fs.read`
- `fs.write`
- `net`
- `search`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | retention ledger | primary | runs |
| FORTRAN | `trebago` | numeric | decay risk model | primary | runs |
| Ada | `eve` | limits | retention limits | primary | runs |
| APL | `kron` | arrays | copy integrity grid | primary | runs |
| MUMPS | `myra` | records | archive hierarchy | primary | runs |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Haskell): see docs/LANGUAGE_CATALOGUE.md for this tier
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `copies_ok` (at least the declared copies, on at least two kinds of media, each verified within its interval) in Ada as Eve + APL as KRON witnessed
- `decay_risk` (months since the last verify x the media's declared risk) in FORTRAN as Trebago + APL as KRON witnessed
- `retention_ledger` (copies held per record class, refused on overflow) in COBOL as Sutra
- `integrity` grid (copy by check) in APL as KRON
- `archive` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id cupola
matrix 282
name Cupola
needs backup
needs fs.read
needs fs.write
needs net
needs search
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

