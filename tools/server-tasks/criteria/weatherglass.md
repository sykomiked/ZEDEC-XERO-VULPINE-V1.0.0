# Criteria — weatherglass

| | |
|---|---|
| app | `weatherglass` |
| matrix layer | 88 |
| icon group | L1 |
| wave spec | `wave8/088-weatherglass.md` |

## Purpose

local conditions and forecast for where the machine is (`place.here`, `place.ask`). There is no weather source: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
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
| Kotlin | kotlin | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id weatherglass
matrix 88        # the graph's slot IS the matrix layer
name Weatherglass
needs location
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

