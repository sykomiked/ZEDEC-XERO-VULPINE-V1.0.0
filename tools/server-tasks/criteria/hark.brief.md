# hark — the complete brief

| | |
|---|---|
| matrix layer | 134 |
| icon group | L1 |
| wave spec | `wave11/134-hark.md` |

## Purpose

run commands by voice; free requests go to `swarm.ask` and come back as suggestions held for the person; language from `xlate.detect`. zxvd has no speech-to-text method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `mic`
- `ai.ask`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Racket | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Perl | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may run by voice" = known command AND confidence at least the threshold AND not a held kind (trash, empty Trash, send, pay, grant, approve) without a spoken confirm, in eve + kron witnessed
- limits (threshold 50..100, confirm window 3..30 s) in eve
- command phrase table (phrase, command, kind) in myra
- Voice never approves money, grants, deletions or access by itself
- those need the person's second confirm

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id hark
matrix 134
name Hark
needs mic
needs ai.ask
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

