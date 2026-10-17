# belvedere — the complete brief

| | |
|---|---|
| matrix layer | 87 |
| icon group | L1 |
| wave spec | `wave8/087-belvedere.md` |

## Purpose

the public face of your space (`spaces.create`, `spaces.get`, `spaces.set_cid`, `pspace.create`, `files.cross`, `web4.cid`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `peers`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| TypeScript | `typescript` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |
| Logo | `logo` | geometry | geometry | primary | runs |
| Java | `java` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **typescript** (TypeScript): the host tier: the browser surface, typed.
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.
- **java** (Java): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may this file go on the cloud top" (a copy into a more open kind needs the owner's confirmation) in eve + myra witnessed
- page byte totals exact in trebago
- published items and their CIDs in myra (CIDs come from the daemon only)
- Nothing is published without the owner's confirmation

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## The manifest contract

```
zxapp 1
id belvedere
matrix 87
name Belvedere
needs fs.read
needs fs.write
needs peers
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

