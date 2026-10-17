# halloo — the complete brief

| | |
|---|---|
| matrix layer | 28 |
| icon group | L1 |
| wave spec | `wave3/028-halloo.md` |

## Purpose

place, answer and end calls (`call.start`, `call.accept`, `call.hangup`, `call.status`, `call.signal`). Media (capture, codecs, RTP) is not wired (H5): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `mic`
- `camera`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- "may this call ring now" (known contact, not blocked, quiet hours as minutes of day) in eve + kron witnessed
- ring timeout limits in eve
- call duration (ms to h:m:s) in kron
- call history record in myra
- Say on screen that Halloo cannot reach emergency services. Signalling travels only inside an encrypted session

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id halloo
matrix 28
name Halloo
needs peers
needs mic
needs camera
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

