# Criteria — seedbank

| | |
|---|---|
| app | `seedbank` |
| matrix layer | 33 |
| icon group | L1 |
| wave spec | `wave1/033-seedbank.md` |

## Purpose

pin and publish by CID (`aipi.put`, `aipi.status`, `web4.cid`), fetch (`aipi.get`), record a published space's CID (`spaces.set_cid`). Unpinning and announcing to the network have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| Elixir | - | messaging |
| TeX | - | typesetting |
| Smalltalk | - | view and model |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id seedbank
matrix 33        # the graph's slot IS the matrix layer
name Seedbank
needs net
needs fs.read
needs fs.write
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

