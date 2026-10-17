# steady — the complete brief

| | |
|---|---|
| matrix layer | 140 |
| icon group | L1 |
| wave spec | `wave11/140-steady.md` |

## Purpose

sticky keys, slow keys, bounce keys, mouse keys. zxvd has no input-filter method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Racket | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Racket): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- key accepted (held at least the slow delay AND not a repeat inside the bounce window) in eve + kron witnessed
- sticky modifier step (off, latched, locked) in eve
- limits (delays 0..2000 ms, mouse-key speed 1..50 px a tick) in eve
- profile record in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id steady
matrix 140
name Steady
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

