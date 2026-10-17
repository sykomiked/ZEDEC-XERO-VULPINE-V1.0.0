# gazette — the complete brief

| | |
|---|---|
| matrix layer | 36 |
| icon group | L1 |
| wave spec | `wave5/036-gazette.md` |

## Purpose

read what nodes and people post (`social.feed`, `chat.feed`, `web4.feed`, `pspace.events`, `irc.record`); follow sources (`chat.follow`). Fetching outside syndication feeds has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| C++ | `cpp` | money | money and ledgers | witness | runs (compiled to wasm) |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **cpp** (C++): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "show this item" (source followed, not muted) in eve + kron witnessed
- unread counts per source in kron + trebago
- read-state index in myra
- Keep the daemon's ranking
- never rank by engagement

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id gazette
matrix 36
name Gazette
needs net
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

