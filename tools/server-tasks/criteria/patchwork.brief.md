# patchwork — the complete brief

| | |
|---|---|
| matrix layer | 388 |
| icon group | L3 |
| wave spec | `wave32/388-patchwork.md` |

## Purpose

Gathers a family's history: stories, photos, records and voices in the Hoard (`files.read`, `files.write`, `search.query`), versions (`backup.versions`), the family space (`spaces.get`, `spaces.set_members`), published to the family by CID (`aipi.put`, `web4.cid`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `backup`
- `peers`
- `net`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | archive storage costs | primary | runs |
| FORTRAN | `trebago` | numeric | date estimation | primary | runs |
| Ada | `eve` | limits | privacy limits | primary | runs |
| APL | `kron` | arrays | timeline grid | primary | runs |
| MUMPS | `myra` | records | family tree | primary | runs |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Julia): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- - `privacy_ok`: (critical) privacy limits (a living person's records shared only with their consent
- others by the family's rule) in Ada as Eve + APL as KRON witnessed
- - `dates`: (critical) date estimation from family sources (bounds intersected) in FORTRAN as Trebago + APL as KRON witnessed
- - `timeline`: timeline grid in APL as KRON
- - `share_out`: sharing to relatives (who gets which branch) in Ada as Eve
- - `storage`: archive storage costs (bytes and fees, refusing on overflow) in COBOL as Sutra
- - `family`: family tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two relatives give different years for a wedding: the date is BOTH
- the test asserts state 3 and that both accounts are kept. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id patchwork
matrix 388
name Patchwork
needs fs.read
needs fs.write
needs search
needs backup
needs peers
needs net
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

