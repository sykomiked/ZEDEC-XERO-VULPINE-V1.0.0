# meadow — the complete brief

| | |
|---|---|
| matrix layer | 86 |
| icon group | L1 |
| wave spec | `wave2/086-meadow.md` |

## Purpose

find, join and use public spaces (`pspace.find`, `pspace.join`, `pspace.leave`, `pspace.list`, `pspace.members`, `pspace.events`, `pspace.post`, `spaces.list` for park rights). Feature `pspace` is off by default.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |
| C# | `dotnet` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.
- **dotnet** (C#): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may this seat post here" (the space kind's rights, post 1..1024 bytes) in eve + myra witnessed (myra looks the seat's role up in the rights record)
- member and event counts per space in trebago
- joined spaces and the last event index in myra
- Posts cannot be deleted (`pspace.post`): the app says so before sending

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id meadow
matrix 86
name Meadow
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

