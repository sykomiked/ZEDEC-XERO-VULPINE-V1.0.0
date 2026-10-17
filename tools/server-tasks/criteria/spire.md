# Criteria — spire

| | |
|---|---|
| app | `spire` |
| matrix layer | 323 |
| icon group | L3 |
| wave spec | `wave27/323-spire.md` |

## Purpose

manages geology and mining surveys: samples, claims, drill logs, reserves and land restoration (`files.read`, `files.write`, `search.query`, `place.list`, `aipi.execute`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `location`
- `peers`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | royalty accounts |
| FORTRAN | trebago | ore body model |
| Ada | eve | extraction limits |
| APL | kron | drill grid |
| MUMPS | myra | claim hierarchy |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id spire
matrix 323        # the graph's slot IS the matrix layer
name Spire
needs fs.read
needs fs.write
needs search
needs location
needs peers
needs net
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

