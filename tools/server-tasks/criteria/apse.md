# Criteria — apse

| | |
|---|---|
| app | `apse` |
| matrix layer | 337 |
| icon group | L3 |
| wave spec | `wave28/337-apse.md` |

## Purpose

supports faith and spiritual communities: services, readings, member care, giving and quiet reflection (`files.read`, `files.write`, `ledger.entries`, `social.groups`, `pspace.post`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ledger.read`
- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | donations |
| FORTRAN | trebago | calendar calculations |
| Ada | eve | privacy limits |
| APL | kron | service rota grid |
| MUMPS | myra | congregation tree |
| Bash | - | jobs |
| C# | dotnet | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id apse
matrix 337        # the graph's slot IS the matrix layer
name Apse
needs fs.read
needs fs.write
needs ledger.read
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

