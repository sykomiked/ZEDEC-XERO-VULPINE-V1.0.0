# Criteria — spindrift

| | |
|---|---|
| app | `spindrift` |
| matrix layer | 223 |
| icon group | L2 |
| wave spec | `wave18/223-spindrift.md` |

## Purpose

Writes, illustrates and publishes interactive stories: stories and art in the Hoard (`files.read`, `files.write`), AI-drafted passages from `swarm.ask` held for the author, published to IPFS (`aipi.put`, `web4.cid`) and recorded on the author's space (`spaces.get`, `spaces.set_cid`), printed with `print.submit`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `net`
- `ai.ask`
- `print`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | rights records |
| FORTRAN | trebago | layout metrics |
| Ada | eve | content limits |
| APL | kron | branch matrix |
| MUMPS | myra | chapter tree |
| SNOBOL | - | text |
| TeX | - | typesetting |
| Elm | - | view and model |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id spindrift
matrix 223        # the graph's slot IS the matrix layer
name Spindrift
needs fs.read
needs fs.write
needs net
needs ai.ask
needs print
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

