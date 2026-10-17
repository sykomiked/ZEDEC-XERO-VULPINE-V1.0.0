# swage — the complete brief

| | |
|---|---|
| matrix layer | 146 |
| icon group | L2 |
| wave spec | `wave12/146-swage.md` |

## Purpose

shapes source in the Hoard (`files.list`, `files.read`, `files.write`) into a signed, tested, installable package in one pass, reading the machine's facts from `sys.info`. zxvd has no method for build/test run, package sign and install (the same gaps Satchel and Whetstone report): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | release ledger | primary | runs |
| FORTRAN | `trebago` | numeric | build timing | primary | runs |
| Ada | `eve` | limits | declared resource limits | primary | runs |
| APL | `kron` | arrays | dependency matrix | primary | runs |
| MUMPS | `myra` | records | package tree | primary | runs |
| BCPL | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (BCPL): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may ship" = Whetstone's verdict TRUE AND Satchel's limits hold AND both signatures present (0/1 inputs) AND the compiler outputs agree, in Ada as Eve + APL as KRON witnessed
- release ledger (major.minor.patch strictly above the last release, release number refused on overflow) in COBOL as Sutra + APL as KRON witnessed
- build timing (stage milliseconds summed) exact in FORTRAN as Trebago
- dependency order (every dependency built before its dependant, over the dependency matrix) in APL as KRON
- declared resource limits (fuel, memory, files) in Ada as Eve
- package tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## The manifest contract

```
zxapp 1
id swage
matrix 146
name Swage
needs fs.read
needs fs.write
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

