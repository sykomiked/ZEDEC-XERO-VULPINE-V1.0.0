# armature — the complete brief

| | |
|---|---|
| matrix layer | 297 |
| icon group | L3 |
| wave spec | `wave25/297-armature.md` |

## Purpose

an invention lab notebook: experiments, prototypes, parts, results and dated proof of who invented what (`files.read`, `files.write`, `journal.query`, `web4.cid`, `aipi.put`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | prototype costs | primary | runs |
| FORTRAN | `trebago` | numeric | test calculations | primary | runs |
| Ada | `eve` | limits | safety limits | primary | runs |
| APL | `kron` | arrays | result matrix | primary | runs |
| MUMPS | `myra` | records | design revision tree | primary | runs |
| SNOBOL | `-` | text | text | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (SNOBOL): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `proof_ok` (an entry is dated proof only when its content id is on the journal and the chain is intact) in Ada as Eve + APL as KRON witnessed
- `test_calc` (derived results from measurements, reduced rationals) in FORTRAN as Trebago + APL as KRON witnessed
- `prototype_costs` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `results` matrix in APL as KRON
- `revisions` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id armature
matrix 297
name Armature
needs fs.read
needs fs.write
needs journal
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

