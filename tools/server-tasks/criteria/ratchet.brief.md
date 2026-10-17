# ratchet — the complete brief

| | |
|---|---|
| matrix layer | 152 |
| icon group | L2 |
| wave spec | `wave12/152-ratchet.md` |

## Purpose

diagnoses hardware faults from the machine's readings (`sys.info`, `sys.disk`, `power.status`), schedules maintenance and walks a person through a repair step by step. zxvd has no method for readings from machines other than this one: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | parts and labour costs | primary | runs |
| FORTRAN | `trebago` | numeric | wear model | primary | runs |
| Ada | `eve` | limits | safe operating limits | primary | runs |
| APL | `kron` | arrays | sensor grid | primary | runs |
| MUMPS | `myra` | records | machine parts tree | primary | runs |
| Erlang | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "fault" (a reading outside its safe operating limits) in Ada as Eve + APL as KRON witnessed
- next repair step from the check results in BASIC (twin Hatchling) + APL as KRON witnessed (the person sees one step at a time)
- wear (hours run x load share, summed) and hours to the next service exact in FORTRAN as Trebago
- parts and labour costs in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- sensor grid (sensor by hour) in APL as KRON
- machine parts tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id ratchet
matrix 152
name Ratchet
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

