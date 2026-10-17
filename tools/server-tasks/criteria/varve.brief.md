# varve — the complete brief

| | |
|---|---|
| matrix layer | 243 |
| icon group | L2 |
| wave spec | `wave20/243-varve.md` |

## Purpose

Builds history timelines from archives, sources and dated events: the Hoard (`files.list`, `files.read`, `files.write`, `search.query`), versions (`backup.versions`), the companion's reading only with Lore's leave (`swarm.ask`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `ai.ask`
- `backup`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | archive catalogue | primary | runs |
| FORTRAN | `trebago` | numeric | date estimation | primary | runs |
| Ada | `eve` | limits | source rules | primary | runs |
| APL | `kron` | arrays | event matrix | primary | runs |
| MUMPS | `myra` | records | era tree | primary | runs |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Icon | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JCL): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- - (critical) date shown for an event (each source's earliest and latest day number intersected
- circa ranges kept) in FORTRAN as Trebago + APL as KRON witnessed
- - source rules (a source counts when dated and cited
- primary or secondary marked) in Ada as Eve
- - event matrix (events x sources) in APL as KRON
- - archive catalogue and era tree in MUMPS as MYRA
- - Sutra is left out: no money or counted business records
- the catalogue is MYRA's
- Paraconsistent (BOTH test, in test_zxapp.c): source A dates an event to 1066 and source B to 1067 with no overlap, giving BOTH
- the test asserts state 3 and that both dates stay on the timeline. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id varve
matrix 243
name Varve
needs fs.read
needs fs.write
needs search
needs ai.ask
needs backup
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

