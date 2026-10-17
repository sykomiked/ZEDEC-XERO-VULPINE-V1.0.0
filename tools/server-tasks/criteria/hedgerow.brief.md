# hedgerow — the complete brief

| | |
|---|---|
| matrix layer | 30 |
| icon group | L1 |
| wave spec | `wave2/030-hedgerow.md` |

## Purpose

who may reach this machine: network mode (`net.command` off/lan/online), answering peers (`peer.serve`), the phone link (`phone.link`, `phone.list`), switches that expose methods (`features.get`), hosted spaces (`pspace.list`) and the IRC bridge (`irc.status`). Packet filtering and per-port rules have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| HyperTalk | `-` | view | view and model | primary | planned: needs Lingua front-end |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "is this path reachable" (mode, switch, private bind) in eve + kron + C++ wasm (three witnesses)
- address class (loopback, private, link-local, unique-local, from octets) in Rust wasm + kron witnessed
- rule record in myra
- Opening anything is asked first
- closing is at once

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id hedgerow
matrix 30
name Hedgerow
needs net
needs peers
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

