# Criteria — caravan

| | |
|---|---|
| app | `caravan` |
| matrix layer | 90 |
| icon group | L1 |
| wave spec | `wave1/090-caravan.md` |

## Purpose

shipments, parcels and addresses (`place.list`, records in the Banker via `files.read`, `files.write`). No shipment or carrier method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
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
| Objective-C | - | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id caravan
matrix 90        # the graph's slot IS the matrix layer
name Caravan
needs fs.read
needs fs.write
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

