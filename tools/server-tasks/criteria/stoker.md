# Criteria — stoker

| | |
|---|---|
| app | `stoker` |
| matrix layer | 196 |
| icon group | L2 |
| wave spec | `wave16/196-stoker.md` |

## Purpose

runs models on the fastest hardware present, GPU first: picks the backend, places weights in video memory, checks each model's signature and licence (`model.info`), and reports speed per watt (`power.status`, `power.top`, `sys.info`, `swarm.state`). zxvd has no method for GPU and backend enumeration, video-memory placement: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `ai.ask`
- `sys.read`
- `swarm`
- `fs.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | model admission log |
| FORTRAN | trebago | integer kernels |
| Ada | eve | refuses unsigned or non-commercial weights |
| APL | kron | tensor layouts |
| MUMPS | myra | model catalogue |
| Python | python | AI |
| B | - | low-level system |
| Simula I | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id stoker
matrix 196        # the graph's slot IS the matrix layer
name Stoker
needs ai.ask
needs sys.read
needs swarm
needs fs.read
provides <entry> num|truth <arg>[,<arg>...]
logic <lang> <entry> <<
<source>
.
check <entry> <arg>=<int>[,<int>...] = <answer>[,<answer>...]
```

Rules the loader enforces (a violation refuses the manifest):

- Every `provides` entry must have at least one `logic` line.
- Every `logic` entry must have a `check` line with exact answers.
- The checks' inputs are integers; answers are integers, `n/d`,
  `true`, `false`, `neither`, or the word `both` (two languages
  disagreeing).
- A truth entry's checks use the four-valued answers, never 0/1.
- Inline sources end with a line containing only `.`.

