# nightjar — the complete brief

| | |
|---|---|
| matrix layer | 133 |
| icon group | L1 |
| wave spec | `wave4/133-nightjar.md` |

## Purpose

speak what is on screen and where focus is; language of text from `xlate.detect`, simple mode from `ui.get`. zxvd has no accessibility-tree or speech method (kernel/src/speech is not exposed): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| HyperTalk | `-` | view | view and model | primary | planned: needs Lingua front-end |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (OCaml): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- "interrupt now" (alerts and focus changes cut in, page reading does not) in eve + kron witnessed
- speech limits (80..450 words a minute, pitch 50..200) in eve
- element record (role, name, row, column, state) in myra
- reading order (by row, then column) in kron

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id nightjar
matrix 133
name Nightjar
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

