# weatherglass — the complete brief

| | |
|---|---|
| matrix layer | 88 |
| icon group | L1 |
| wave spec | `wave8/088-weatherglass.md` |

## Purpose

local conditions and forecast for where the machine is (`place.here`, `place.ask`). There is no weather source: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |
| Kotlin | `kotlin` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.
- **kotlin** (Kotlin): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- unit conversion (tenths of a degree C to F, mm, km/h) in trebago + kron witnessed
- frost, heat and wind warnings (integer thresholds) in eve + trebago witnessed
- 7-day min, max, mean floor in trebago
- readings in myra
- Warnings are advisory and never replace official warnings

## The entries and their exact checks

(no library pattern matched; author the entries from the clauses)

## The manifest contract

```
zxapp 1
id weatherglass
matrix 88
name Weatherglass
needs location
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

