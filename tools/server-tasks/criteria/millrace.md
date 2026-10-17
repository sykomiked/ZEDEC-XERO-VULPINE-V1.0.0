# Criteria — millrace

| | |
|---|---|
| app | `millrace` |
| matrix layer | 197 |
| icon group | L2 |
| wave spec | `wave16/197-millrace.md` |

## Purpose

builds data pipelines that branch, merge and route streams between apps on a schedule, from the Hoard (`files.read`, `files.write`), with the daemon's methods (`rpc.discover`), events (`log.tail`) and rates (`sys.net`). zxvd has no method for a pipeline routing method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | batch records |
| FORTRAN | trebago | flow rates |
| Ada | eve | back-pressure limits |
| APL | kron | stream arrays |
| MUMPS | myra | pipeline tree |
| Bourne shell | - | jobs |
| Clojure | - | dataflow |
| Prolog | - | search |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id millrace
matrix 197        # the graph's slot IS the matrix layer
name Millrace
needs sys.read
needs fs.read
needs fs.write
needs journal
needs notify
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

