# pivot — the complete brief

| | |
|---|---|
| matrix layer | 136 |
| icon group | L1 |
| wave spec | `wave11/136-pivot.md` |

## Purpose

drive the machine with one or two switches (scanning) or the eyes (dwell to click). zxvd has no input-device or gaze method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `camera`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| R | `-` | statistics | statistics | primary | planned: needs Lingua front-end |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (R): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- dwell select (every gaze sample inside the target for at least the dwell time) in eve + kron witnessed
- limits (dwell 200..5000 ms, scan step 300..10000 ms, target at least 44 px) in eve
- profile record (switches, dwell, scan order) in myra
- A dwell never confirms a held action (pay, grant, trash)
- those need a second dwell on a confirm target

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id pivot
matrix 136
name Pivot
needs camera
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

