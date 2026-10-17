# seedbank — the complete brief

| | |
|---|---|
| matrix layer | 33 |
| icon group | L1 |
| wave spec | `wave1/033-seedbank.md` |

## Purpose

pin and publish by CID (`aipi.put`, `aipi.status`, `web4.cid`), fetch (`aipi.get`), record a published space's CID (`spaces.set_cid`). Unpinning and announcing to the network have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `net`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |
| Smalltalk | `-` | view | view and model | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- CID string check in Rust wasm + kron witnessed (the same rule as Ramble's
- copy it as a block)
- size limits (put 1 MiB, get 65536 bytes) in eve
- blockstore use x100 from the used_share block (`use used_share 1`)
- pin record in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id seedbank
matrix 33
name Seedbank
needs net
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

