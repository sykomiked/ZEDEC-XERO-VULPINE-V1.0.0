# gibbous — the complete brief

| | |
|---|---|
| matrix layer | 283 |
| icon group | L3 |
| wave spec | `wave23/283-gibbous.md` |

## Purpose

tracks sleep, light exposure and body clock, suggesting gentle routines for better rest (advice only), from the Hoard (`files.read`, `files.write`), `screen.status` and `sys.info`. zxvd has no method for wearable and light sensor readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `screen.time`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | wellness plan costs | primary | runs |
| FORTRAN | `trebago` | numeric | circadian model | primary | runs |
| Ada | `eve` | limits | screen and alarm limits | primary | runs |
| APL | `kron` | arrays | night sensor arrays | primary | runs |
| MUMPS | `myra` | records | sleep history | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `routine_ok` (a suggested bedtime shift at most 30 minutes a week, no alarm before the declared wake time, screen limits kept) in Ada as Eve + APL as KRON witnessed
- `circadian` (sleep midpoint and its drift over 14 nights) in FORTRAN as Trebago + APL as KRON witnessed
- `plan_cost` (a wellness plan's items in minor units) in COBOL as Sutra
- `night` sensor arrays in APL as KRON
- `sleep_history` in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id gibbous
matrix 283
name Gibbous
needs fs.read
needs fs.write
needs sys.read
needs screen.time
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

