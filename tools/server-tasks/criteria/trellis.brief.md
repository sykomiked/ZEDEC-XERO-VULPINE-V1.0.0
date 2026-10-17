# trellis — the complete brief

| | |
|---|---|
| matrix layer | 29 |
| icon group | L1 |
| wave spec | `wave1/029-trellis.md` |

## Purpose

pair (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), trust and revoke (`devmesh.devices`, `devmesh.status`, `devmesh.revoke`), phones (`phone.pair`, `phone.list`, `phone.revoke`), peers (`net.peers`, `net.command` off/lan/online).

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
| Elm | `-` | view | view and model | primary | planned: needs Lingua front-end |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Elm): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may this device be revoked" (admin session, not this device, not the last admin) in eve + kron witnessed
- invitation minutes left in kron
- peers per state (active, revoked, waiting) in kron + trebago
- peer record in myra
- A revocation can never be undone: the owner confirms it twice. Tests never go online

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id trellis
matrix 29
name Trellis
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

