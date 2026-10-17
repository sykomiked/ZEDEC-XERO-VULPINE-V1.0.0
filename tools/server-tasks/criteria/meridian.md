# Criteria — meridian

| | |
|---|---|
| app | `meridian` |
| matrix layer | 291 |
| icon group | L3 |
| wave spec | `wave24/291-meridian.md` |

## Purpose

plans voyages by sea or land: routes, tides, weather windows, fuel and crew watches (`place.here`, `place.list`, `place.add`, `files.read`, `files.write`). zxvd has no method for weather and tide sources: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | voyage accounts |
| FORTRAN | trebago | tide and current model |
| Ada | eve | weather go/no-go limits |
| APL | kron | route grid |
| MUMPS | myra | crew and vessel records |
| Prolog | - | search |
| Simula 67 | - | simulation |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id meridian
matrix 291        # the graph's slot IS the matrix layer
name Meridian
needs location
needs fs.read
needs fs.write
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

