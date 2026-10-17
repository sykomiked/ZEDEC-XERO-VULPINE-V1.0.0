# cove — the complete brief

| | |
|---|---|
| matrix layer | 32 |
| icon group | L1 |
| wave spec | `wave5/032-cove.md` |

## Purpose

only between devices on the owner's roster (`devmesh.devices`, `devmesh.status`); frames sealed and opened with `ehop.seal`, `ehop.open` on a band from `ehop.bands`. No carrier is wired to the socket (H7): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| System/360 assembler | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| Objective-C | `-` | view | view and model | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (System/360 assembler): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may this tunnel open" (both devices active, neither revoked) in eve + kron witnessed
- nonce discipline (seq strictly rising per channel, refusal on reuse or overflow) in sutra + Rust wasm witnessed
- frame width within the band's largest frame in eve
- tunnel record in myra
- Never log or store key bytes
- tests use fixed test-vector keys only. Never copy anything from build_system/keys/

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id cove
matrix 32
name Cove
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

