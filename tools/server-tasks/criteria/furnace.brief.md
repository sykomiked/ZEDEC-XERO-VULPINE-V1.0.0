# furnace — the complete brief

| | |
|---|---|
| matrix layer | 199 |
| icon group | L2 |
| wave spec | `wave16/199-furnace.md` |

## Purpose

runs a home or village microgrid: solar output, storage and when to use power, from `power.status`, `power.top`, `power.budget` and `sys.info`. zxvd has no method for solar inverter, meter and battery bank readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `notify`
- `net`
- `peers`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | energy accounts | primary | runs |
| FORTRAN | `trebago` | numeric | solar model | primary | runs |
| Ada | `eve` | limits | grid limits | primary | runs |
| APL | `kron` | arrays | load grid | primary | runs |
| MUMPS | `myra` | records | site tree | primary | runs |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (IEC 61131-3 Structured Text): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- energy accounts (Wh x tariff in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- expected solar Wh (panel watts x sun minutes / 60) in FORTRAN as Trebago + APL as KRON witnessed
- "within grid limits" (draw at most the inverter's maximum AND battery at least the reserve AND export at most the allowance) in Ada as Eve + APL as KRON witnessed
- load grid in APL as KRON
- site tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |

## The manifest contract

```
zxapp 1
id furnace
matrix 199
name Furnace
needs sys.read
needs notify
needs net
needs peers
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

