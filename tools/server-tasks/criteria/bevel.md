# Criteria — bevel

| | |
|---|---|
| app | `bevel` |
| matrix layer | 318 |
| icon group | L3 |
| wave spec | `wave26/318-bevel.md` |

## Purpose

finds and redacts personal data in documents and datasets before sharing, keeping only what is needed (`files.read`, `files.write`, `search.query`, `journal.query`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `journal`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | disclosure log |
| FORTRAN | trebago | re-identification risk |
| Ada | eve | retention limits |
| APL | kron | field sensitivity grid |
| MUMPS | myra | record hierarchy |
| SQL | - | queries |
| Prolog | - | search |
| Icon | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id bevel
matrix 318        # the graph's slot IS the matrix layer
name Bevel
needs fs.read
needs fs.write
needs search
needs journal
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

