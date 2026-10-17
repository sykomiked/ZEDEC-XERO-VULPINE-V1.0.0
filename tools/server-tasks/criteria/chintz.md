# Criteria — chintz

| | |
|---|---|
| app | `chintz` |
| matrix layer | 346 |
| icon group | L3 |
| wave spec | `wave29/346-chintz.md` |

## Purpose

Identifies, catalogues and monitors plants in the field or greenhouse: photos and labels in the Hoard (`files.read`, `files.write`, `search.query`), the place after `place.ask`, labels printed (`print.submit`), identification suggestions from `swarm.ask` (held).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `location`
- `camera`
- `print`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | herbarium loan records |
| FORTRAN | trebago | growth curves |
| Ada | eve | pesticide limits |
| APL | kron | trait matrix |
| MUMPS | myra | taxonomy tree |
| Julia | - | matrices and images |
| Forth | forth | machine control |
| Mojo | - | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id chintz
matrix 346        # the graph's slot IS the matrix layer
name Chintz
needs fs.read
needs fs.write
needs search
needs location
needs camera
needs print
needs ai.ask
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

