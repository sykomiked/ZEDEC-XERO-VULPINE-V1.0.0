# Criteria — archivolt

| | |
|---|---|
| app | `archivolt` |
| matrix layer | 322 |
| icon group | L3 |
| wave spec | `wave27/322-archivolt.md` |

## Purpose

migrates old systems to ZXV: reads mainframe data and code, translates it, and runs old and new side by side (`files.read`, `files.write`, `play.run`, `xlate.detect`, `net.command`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ai.ask`
- `net`
- `peers`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | COBOL record mapping |
| FORTRAN | trebago | numeric parity checks |
| Ada | eve | cutover limits |
| APL | kron | field mapping grid |
| MUMPS | myra | legacy hierarchy |
| OCaml | - | parsing |
| Python | python | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id archivolt
matrix 322        # the graph's slot IS the matrix layer
name Archivolt
needs fs.read
needs fs.write
needs ai.ask
needs net
needs peers
needs sys.read
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

