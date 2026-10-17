# caravan — the complete brief

| | |
|---|---|
| matrix layer | 90 |
| icon group | L1 |
| wave spec | `wave1/090-caravan.md` |

## Purpose

shipments, parcels and addresses (`place.list`, records in the Banker via `files.read`, `files.write`). No shipment or carrier method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `location`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |
| Objective-C | `-` | objects | domain objects | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.
- **-** (Objective-C): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- charged weight = max(mass, ceil(L x W x H / divisor)) in trebago + sutra witnessed
- "within carrier limits" (mass, length, girth) in eve + trebago witnessed
- route choice from `use pce_strat 1` (C, advisory)
- shipment records in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id caravan
matrix 90
name Caravan
needs fs.read
needs fs.write
needs location
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

