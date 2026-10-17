# conch — the complete brief

| | |
|---|---|
| matrix layer | 141 |
| icon group | L1 |
| wave spec | `wave11/141-conch.md` |

## Purpose

pair hearing devices, balance and mono, show sound alerts as a screen flash; alerts from `notes.list`. zxvd has no audio-device method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `notify`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Raku | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- output level within the cap (per-band gain at most +30 dB in tenths, total at most the owner's cap) in eve + kron witnessed
- balance limits (-100..100) in eve
- alert mapping (sound kind, flash, vibrate) in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id conch
matrix 141
name Conch
needs notify
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

