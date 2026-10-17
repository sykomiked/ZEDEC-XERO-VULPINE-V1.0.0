# wisp — the complete brief

| | |
|---|---|
| matrix layer | 217 |
| icon group | L2 |
| wave spec | `wave18/217-wisp.md` |

## Purpose

Keeps light, offline maps, contacts and battery triage ready when power or network fails: battery, draw and source from `power.status` and `sys.info`; network state from `sys.net`; where the machine is and the saved places (`place.here`, `place.list`); emergency contacts and the medical ID from Watchfire's records (`files.read`); a low-power start with every optional switch off (`features.get`, `features.set`, owner-approved); ring a contact only on the person's request (`call.start`, signalling only). Offline map tiles and outside outage reports have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `location`
- `peers`
- `fs.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | supply list | primary | runs |
| FORTRAN | `trebago` | numeric | battery runtime model | primary | runs |
| Ada | `eve` | limits | power reserve limits | primary | runs |
| APL | `kron` | arrays | priority grid | primary | runs |
| MUMPS | `myra` | records | household medical tree | primary | runs |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| C# | `dotnet` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier
- **dotnet** (C#): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "keep this function on" (light, contacts, map, radio, each with its draw in mW) = the reserve after it stays above the floor the owner set for contacts and light, in Ada as Eve + APL as KRON witnessed
- - battery runtime per plan (charge in mWh / summed draw, minutes, exact) in FORTRAN as Trebago, checked against Brazier's minutes left
- - priority grid (functions x priority rank, the order they are shed) in APL as KRON
- - supply list as fixed records (item, count, litres or units), counts refusing on overflow, in COBOL as Sutra
- - household medical tree (person, needs, medicines, stored as typed, never inferred) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): mains present from `power.status` (1) and a household outage report (0) give BOTH
- the test feeds exactly that pair and asserts state 3, the low-power plan shown, and no switch changed without the person. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id wisp
matrix 217
name Wisp
needs sys.read
needs location
needs peers
needs fs.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

