# Criteria — bonfire

| | |
|---|---|
| app | `bonfire` |
| matrix layer | 95 |
| icon group | L1 |
| wave spec | `wave8/095-bonfire.md` |

## Purpose

events in a public space or group (`pspace.create`, `pspace.post`, `pspace.members`, `social.group_create`, `place.list`). No calendar or RSVP method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `location`
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
| Smalltalk | - | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id bonfire
matrix 95        # the graph's slot IS the matrix layer
name Bonfire
needs peers
needs net
needs location
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

