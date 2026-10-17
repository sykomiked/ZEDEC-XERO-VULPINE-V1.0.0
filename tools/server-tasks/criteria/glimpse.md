# Criteria — glimpse

| | |
|---|---|
| app | `glimpse` |
| matrix layer | 111 |
| icon group | L1 |
| wave spec | `wave9/111-glimpse.md` |

## Purpose

capture the screen, a window or a region, record the screen; save into the Hoard with `files.write` ("Screenshot N.png", first free N from `files.list`); cut recordings with `stream.plan`. zxvd has no display-capture method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| FORTRAN | trebago | exact numerics |
| APL | kron | arrays |
| Visual Basic | dotnet | view and model |
| dBase | - | records |
| ALGOL 68 | - | formulas |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id glimpse
matrix 111        # the graph's slot IS the matrix layer
name Glimpse
needs fs.read
needs fs.write
needs sys.read
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

