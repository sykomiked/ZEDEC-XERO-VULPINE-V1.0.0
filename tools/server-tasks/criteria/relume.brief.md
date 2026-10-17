# relume — the complete brief

| | |
|---|---|
| matrix layer | 273 |
| icon group | L3 |
| wave spec | `wave23/273-relume.md` |

## Purpose

brings a business or household back after a disaster: restores files, books and services in the right order from signed backups (`backup.list`, `backup.verify`, `backup.restore`), checking the books (`ledger.check`) and the journal (`journal.verify`).

## Capabilities (the exact registry names for `needs`)

- `backup`
- `fs.read`
- `fs.write`
- `journal`
- `ledger.read`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | restored ledgers | primary | runs |
| FORTRAN | `trebago` | numeric | recovery-time model | primary | runs |
| Ada | `eve` | limits | restore order limits | primary | runs |
| APL | `kron` | arrays | service dependency grid | primary | runs |
| MUMPS | `myra` | records | record hierarchy | primary | runs |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `restore_next` (the next item to restore: every item it depends on already restored and verified) in Ada as Eve + APL as KRON witnessed
- `recovery_min` (minutes to restore = bytes / measured rate, summed along the dependency order) exact in FORTRAN as Trebago
- `restored_books` (each restored ledger's balance equals the backup's, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `service_deps` (service dependency matrix) in APL as KRON
- `records` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id relume
matrix 273
name Relume
needs backup
needs fs.read
needs fs.write
needs journal
needs ledger.read
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

