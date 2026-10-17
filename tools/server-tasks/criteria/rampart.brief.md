# rampart — the complete brief

| | |
|---|---|
| matrix layer | 289 |
| icon group | L3 |
| wave spec | `wave24/289-rampart.md` |

## Purpose

a security operations centre for an organisation: alerts, incidents, responders and signed after-action reports (`journal.query`, `journal.verify`, `log.tail`, `power.top`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `journal`
- `sys.read`
- `notify`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident costs | primary | runs |
| FORTRAN | `trebago` | numeric | risk scoring | primary | runs |
| Ada | `eve` | limits | response playbook limits | primary | runs |
| APL | `kron` | arrays | alert correlation grid | primary | runs |
| MUMPS | `myra` | records | org asset hierarchy | primary | runs |
| RPG | `-` | report | reports | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (RPG): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `playbook_ok` (a response step allowed by the incident's playbook AND approved by the responder on duty) in Ada as Eve + APL as KRON witnessed
- `risk` (likelihood x impact per asset, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `incident_costs` (hours and items in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `correlation` grid (alert by asset by hour) in APL as KRON
- `assets` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id rampart
matrix 289
name Rampart
needs journal
needs sys.read
needs notify
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

