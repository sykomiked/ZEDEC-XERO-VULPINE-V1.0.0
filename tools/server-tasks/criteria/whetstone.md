# Criteria — whetstone

| | |
|---|---|
| app | `whetstone` |
| matrix layer | 125 |
| icon group | L1 |
| wave spec | `wave10/125-whetstone.md` |

## Purpose

run builds and tests and show the verdict. Runs Playground templates with `play.run` (four-valued verdict); reads files with `files.read`. zxvd has no build or test-run method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |
| Ada | eve | limits |
| APL | kron | arrays |
| MUMPS | myra | records |
| JCL | - | jobs |
| Racket | - | parsing |
| F# | dotnet | typed data |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id whetstone
matrix 125        # the graph's slot IS the matrix layer
name Whetstone
needs sys.read
needs fs.read
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

