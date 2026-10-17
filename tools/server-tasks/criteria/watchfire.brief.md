# watchfire — the complete brief

| | |
|---|---|
| matrix layer | 143 |
| icon group | L1 |
| wave spec | `wave11/143-watchfire.md` |

## Purpose

emergency contacts and a medical ID shown on the lock screen; on the person's request, ring contacts (`call.start`, signalling only) and share where the machine is (`place.ask`, `place.here`). Contacts' devices from `phone.list`.

## Capabilities (the exact registry names for `needs`)

- `location`
- `peers`
- `notify`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Swift | `swift` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (OCaml): see docs/LANGUAGE_CATALOGUE.md for this tier
- **swift** (Swift): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- "share location with this contact" = person started the emergency AND owner opted in AND contact listed AND `place.ask` allowed, in eve + kron witnessed
- cancel window before any ring (10..60 s, never skipped) in eve + kron witnessed
- medical ID and contact records in myra, stored as the person typed them, never computed
- Life-safety: advisory and human-approved
- never calls emergency services or anyone by itself
- not a medical device
- nothing clinical is inferred

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id watchfire
matrix 143
name Watchfire
needs location
needs peers
needs notify
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

