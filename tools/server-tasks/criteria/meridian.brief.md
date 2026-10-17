# meridian — the complete brief

| | |
|---|---|
| matrix layer | 291 |
| icon group | L3 |
| wave spec | `wave24/291-meridian.md` |

## Purpose

plans voyages by sea or land: routes, tides, weather windows, fuel and crew watches (`place.here`, `place.list`, `place.add`, `files.read`, `files.write`). zxvd has no method for weather and tide sources: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | voyage accounts | primary | runs |
| FORTRAN | `trebago` | numeric | tide and current model | primary | runs |
| Ada | `eve` | limits | weather go/no-go limits | primary | runs |
| APL | `kron` | arrays | route grid | primary | runs |
| MUMPS | `myra` | records | crew and vessel records | primary | runs |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- `window_go` (wind, wave and visibility inside the declared limits for every leg of the window) in Ada as Eve + APL as KRON witnessed
- `tide_current` (current along a leg from tide times, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `voyage_accounts` (fuel and stores in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `routes` grid in APL as KRON
- `crew_vessel` records in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id meridian
matrix 291
name Meridian
needs location
needs fs.read
needs fs.write
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

