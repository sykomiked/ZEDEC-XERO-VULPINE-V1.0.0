# Criteria — azimuth

| | |
|---|---|
| app | `azimuth` |
| matrix layer | 306 |
| icon group | L3 |
| wave spec | `wave25/306-azimuth.md` |

## Purpose

indoor and outdoor wayfinding with step-by-step directions, accessible routes and offline maps (`place.here`, `place.list`, `place.add`, `files.read`). zxvd has no method for beacon readings, indoor maps: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | venue licences |
| FORTRAN | trebago | positioning maths |
| Ada | eve | accessibility limits |
| APL | kron | floor grid |
| MUMPS | myra | building and floor tree |
| Prolog | - | search |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id azimuth
matrix 306        # the graph's slot IS the matrix layer
name Azimuth
needs location
needs fs.read
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

