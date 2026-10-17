# Criteria — broadsheet

| | |
|---|---|
| app | `broadsheet` |
| matrix layer | 42 |
| icon group | L1 |
| wave spec | `wave6/042-broadsheet.md` |

## Purpose

open documents from the Hoard (`files.read`, base64), print (`print.submit`); annotations go to a separate file beside the original (`files.write`), never into it.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| Perl | - | text |
| PostScript | - | page layout |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id broadsheet
matrix 42        # the graph's slot IS the matrix layer
name Broadsheet
needs fs.read
needs fs.write
needs print
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

