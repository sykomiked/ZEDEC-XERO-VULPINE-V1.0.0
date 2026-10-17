# Criteria — cubby

| | |
|---|---|
| app | `cubby` |
| matrix layer | 103 |
| icon group | L1 |
| wave spec | `wave9/103-cubby.md` |

## Purpose

a child's seat with allowed apps and time (`screen.check`, `screen.status`, `screen.set_limit`, `screen.lock`, `seats.list`).

## Capabilities (needs — the exact registry names)

- `screen.time`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| Pascal | pascal | step-by-step teaching |
| Rust | rust | safety rules |
| Ada | eve | safety rules |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cubby
matrix 103        # the graph's slot IS the matrix layer
name Cubby
needs screen.time
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

