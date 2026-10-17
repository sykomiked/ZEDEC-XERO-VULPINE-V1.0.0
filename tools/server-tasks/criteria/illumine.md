# Criteria — illumine

| | |
|---|---|
| app | `illumine` |
| matrix layer | 313 |
| icon group | L3 |
| wave spec | `wave26/313-illumine.md` |

## Purpose

publishes books from manuscript to finished edition: editing, layout, illustration, rights and sales (`files.read`, `files.write`, `print.submit`, `aipi.put`, `web4.cid`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | royalties |
| FORTRAN | trebago | print cost model |
| Ada | eve | rights limits |
| APL | kron | layout grid |
| MUMPS | myra | chapter and edition tree |
| TeX | - | typesetting |
| Lua | - | scripting |
| AWK | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id illumine
matrix 313        # the graph's slot IS the matrix layer
name Illumine
needs fs.read
needs fs.write
needs print
needs net
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

