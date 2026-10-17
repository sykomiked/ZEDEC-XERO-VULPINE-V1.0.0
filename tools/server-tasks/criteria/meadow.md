# Criteria — meadow

| | |
|---|---|
| app | `meadow` |
| matrix layer | 86 |
| icon group | L1 |
| wave spec | `wave2/086-meadow.md` |

## Purpose

find, join and use public spaces (`pspace.find`, `pspace.join`, `pspace.leave`, `pspace.list`, `pspace.members`, `pspace.events`, `pspace.post`, `spaces.list` for park rights). Feature `pspace` is off by default.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| FORTRAN | trebago | exact numerics |
| Logo | logo | geometry |
| C# | dotnet | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id meadow
matrix 86        # the graph's slot IS the matrix layer
name Meadow
needs peers
needs net
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

