# Criteria — clerestory

| | |
|---|---|
| app | `clerestory` |
| matrix layer | 293 |
| icon group | L3 |
| wave spec | `wave24/293-clerestory.md` |

## Purpose

supports a research project end to end: reading lists, notes, citations, drafts and supervisor feedback in the Hoard (`files.read`, `files.write`, `search.query`), grant spending kept as a Hoard file.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | grant spending |
| FORTRAN | trebago | statistics engine |
| Ada | eve | ethics limits |
| APL | kron | literature matrix |
| MUMPS | myra | chapter hierarchy |
| Mathematica | - | symbolic reasoning |
| Natural | - | queries |
| Gleam | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id clerestory
matrix 293        # the graph's slot IS the matrix layer
name Clerestory
needs fs.read
needs fs.write
needs search
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

