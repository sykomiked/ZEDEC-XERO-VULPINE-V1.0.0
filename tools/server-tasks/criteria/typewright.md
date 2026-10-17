# Criteria — typewright

| | |
|---|---|
| app | `typewright` |
| matrix layer | 99 |
| icon group | L1 |
| wave spec | `wave8/099-typewright.md` |

## Purpose

typing lessons and drills; lessons and results in the Banker (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| Pascal | pascal | step-by-step teaching |
| Mojo | - | AI |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id typewright
matrix 99        # the graph's slot IS the matrix layer
name Typewright
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

