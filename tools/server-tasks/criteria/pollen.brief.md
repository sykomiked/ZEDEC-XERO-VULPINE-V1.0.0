# pollen — the complete brief

| | |
|---|---|
| matrix layer | 31 |
| icon group | L1 |
| wave spec | `wave5/031-pollen.md` |

## Purpose

offer and fetch on the local network (`net.command` lan, share, want; `net.offers`, `net.peers`), chunk with `freight.pack` and rebuild with `freight.unpack`, read and save in the Hoard (`files.read`, `files.write`), out of a space only through `files.cross`.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| JavaScript | `typescript` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |
| Nim | `-` | machine | low-level system | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier
- **typescript** (JavaScript): the host tier: the browser surface, typed.

## The spec's Logic clauses (what each component computes)

- "fits on the receiver" in sutra + kron witnessed
- chunk plan (freights and packets for a size and k) exact in trebago + kron witnessed
- share limits (file size, files per offer, offers open) in eve
- progress x100 from the used_share block (`use used_share 1`)
- share record in myra
- Each file is shared only on the owner's approval
- tests stay on lan or loopback

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id pollen
matrix 31
name Pollen
needs peers
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

