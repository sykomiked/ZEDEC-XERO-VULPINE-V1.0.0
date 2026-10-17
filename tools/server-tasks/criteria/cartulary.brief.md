# cartulary — the complete brief

| | |
|---|---|
| matrix layer | 145 |
| icon group | L2 |
| wave spec | `wave12/145-cartulary.md` |

## Purpose

indexed, versioned records in the Hoard (`files.list`, `files.stat`, `files.read`, `files.write`), found with `search.query`, each version kept with `backup.versions` and `backup.list`, and proven years later against the journal (`journal.query`, `journal.verify`). zxvd has no method for a retention-hold flag in the daemon (kept as a Hoard file instead): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `backup`
- `journal`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | retention schedules | primary | runs |
| FORTRAN | `trebago` | numeric | storage growth model | primary | runs |
| Ada | `eve` | limits | retention limits | primary | runs |
| APL | `kron` | arrays | index grid | primary | runs |
| MUMPS | `myra` | records | record hierarchy | primary | runs |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Haskell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- retention schedule rows (class code, years, months) as fixed business records and days until a version may go (filed day + schedule days - today, refused on overflow) in COBOL as Sutra + APL as KRON witnessed
- "may this version be pruned" = schedule over AND no hold AND `keepsake.keep_ok` in Ada as Eve + APL as KRON witnessed
- retention limits (1 day to 36525 days, a hold always wins) in Ada as Eve
- storage growth (bytes per month as a vector, projected bytes after n months from the mean monthly growth, floor) exact in FORTRAN as Trebago
- records per class per year in APL as KRON (the index grid)
- record hierarchy (class, record, version, filed day, hold) in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id cartulary
matrix 145
name Cartulary
needs fs.read
needs fs.write
needs search
needs backup
needs journal
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

