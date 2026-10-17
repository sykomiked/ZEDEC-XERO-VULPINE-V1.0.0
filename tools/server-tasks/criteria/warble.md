# Criteria — warble

| | |
|---|---|
| app | `warble` |
| matrix layer | 50 |
| icon group | L1 |
| wave spec | `wave6/050-warble.md` |

## Purpose

the library from the Hoard (`files.list`, `files.stat`, `search.query`), playing together in a watch hall (`hall.load`, `hall.play`, `hall.pause`, `hall.status`). Local audio output has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `search`
- `peers`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| FORTRAN | trebago | exact numerics |
| LabVIEW | - | signals |
| Julia | - | matrices and images |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id warble
matrix 50        # the graph's slot IS the matrix layer
name Warble
needs fs.read
needs search
needs peers
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

