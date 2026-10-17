# Criteria — fieldguide

| | |
|---|---|
| app | `fieldguide` |
| matrix layer | 102 |
| icon group | L1 |
| wave spec | `wave9/102-fieldguide.md` |

## Purpose

articles in a Hoard folder found by `search.query` (`search.refresh`, `files.read`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `search`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| Python | python | AI |
| BASIC | basic | step-by-step teaching |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id fieldguide
matrix 102        # the graph's slot IS the matrix layer
name Fieldguide
needs fs.read
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

