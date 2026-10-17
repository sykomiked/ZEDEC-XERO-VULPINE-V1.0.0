# glazier — the complete brief

| | |
|---|---|
| matrix layer | 193 |
| icon group | L2 |
| wave spec | `wave16/193-glazier.md` |

## Purpose

publishes budgets and decisions from the books (`ledger.accounts`, `ledger.entries`, `ledger.check`) openly (`aipi.put`, `web4.cid`, `spaces.set_cid`, `files.write`) so anyone can verify them, with the journal checked by `journal.verify`.

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `fs.read`
- `fs.write`
- `net`
- `peers`
- `journal`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | public accounts | primary | runs |
| FORTRAN | `trebago` | numeric | spending trends | primary | runs |
| Ada | `eve` | limits | disclosure rules | primary | runs |
| APL | `kron` | arrays | budget grid | primary | runs |
| MUMPS | `myra` | records | agency tree | primary | runs |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

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

- public accounts per line in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- month-on-month change per line in FORTRAN as Trebago + APL as KRON witnessed
- "may publish" (marked public AND no personal fields AND approved by an owner) in Ada as Eve + APL as KRON witnessed
- budget grid in APL as KRON
- agency tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id glazier
matrix 193
name Glazier
needs ledger.read
needs fs.read
needs fs.write
needs net
needs peers
needs journal
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

