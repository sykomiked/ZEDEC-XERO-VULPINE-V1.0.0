# commons — the complete brief

| | |
|---|---|
| matrix layer | 27 |
| icon group | L1 |
| wave spec | `wave2/027-commons.md` |

## Purpose

rooms on this node (`chat.post`, `chat.feed`, `chat.thread`, `chat.react`, `chat.follow`, `chat.me`), groups (`social.groups`, `social.group_create`, `social.join`, `social.leave`, `social.post`), rooms across nodes (`pspace.create`, `pspace.join`, `pspace.post`, `pspace.events`, `pspace.members`) and the IRC bridge (`irc.status`, `irc.bridge`, `irc.record`). Direct messages have no method of their own: use only existing methods; list what's missing in your report.

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
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Icon | `-` | text | text | primary | planned: needs Lingua front-end |
| Java | `java` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier
- **java** (Java): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may I post here" (member, room joined, switch on) in eve + kron witnessed
- text limits (1..1024 bytes, reaction 1..32) in eve
- unread counts per room in kron + trebago
- room and message index in myra
- Feeds keep the daemon's ranking
- nothing is re-ranked by likes or views

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id commons
matrix 27
name Commons
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

