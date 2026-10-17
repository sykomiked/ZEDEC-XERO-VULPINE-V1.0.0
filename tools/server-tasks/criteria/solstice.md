# Criteria — solstice

| | |
|---|---|
| app | `solstice` |
| matrix layer | 43 |
| icon group | L1 |
| wave spec | `wave6/043-solstice.md` |

## Purpose

events as files in the Hoard (`files.list`, `files.read`, `files.write`). Time zones and a notice the app can post have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| Raku | - | text |
| TeX | - | typesetting |
| PostScript | - | page layout |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id solstice
matrix 43        # the graph's slot IS the matrix layer
name Solstice
needs fs.read
needs fs.write
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

