# orlop — the complete brief

| | |
|---|---|
| matrix layer | 398 |
| icon group | L3 |
| wave spec | `wave33/398-orlop.md` |

## Purpose

Stores and queries your structured data with versioned tables, signed changes and offline copies: tables in the Hoard (`files.read`, `files.write`, `files.list`), versions (`backup.versions`, `backup.list`), the change record (`journal.query`, `journal.verify`), keys in escrow (`recovery.status`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `backup`
- `journal`
- `peers`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | record schemas | primary | runs |
| FORTRAN | `trebago` | numeric | query statistics | primary | runs |
| Ada | `eve` | limits | quota limits | primary | runs |
| APL | `kron` | arrays | column arrays | primary | runs |
| MUMPS | `myra` | records | hierarchical keys | primary | runs |
| F# | `dotnet` | types | typed data | primary | planned: SDK host only, no in-app runtime yet |
| SQL | `-` | query | queries | primary | planned: needs Lingua front-end |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **dotnet** (F#): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (SQL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `write_ok`: (critical) quota limits (a write within the table's row and byte quota AND the schema's field types) in Ada as Eve + APL as KRON witnessed
- - `schema_ok`: (critical) record schemas (fixed field widths, a value that does not fit is refused) in COBOL as Sutra + APL as KRON witnessed
- - `query`: column query over arrays (filters and sums) in APL as KRON
- - `stats`: query statistics (rows scanned, time) exact in FORTRAN as Trebago
- - `keys`: hierarchical keys in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two offline copies write different values to one cell: the cell is BOTH
- the test asserts state 3 and that both values are stored for a later merge. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id orlop
matrix 398
name Orlop
needs fs.read
needs fs.write
needs backup
needs journal
needs peers
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

