# Criteria — lunette

| | |
|---|---|
| app | `lunette` |
| matrix layer | 319 |
| icon group | L3 |
| wave spec | `wave26/319-lunette.md` |

## Purpose

records life ceremonies such as weddings, namings and funerals: bookings, officiants, certificates and guests (`files.read`, `files.write`, `social.groups`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | ceremony fees |
| FORTRAN | trebago | guest capacity |
| Ada | eve | legal notice periods |
| APL | kron | venue calendar grid |
| MUMPS | myra | family tree |
| RPG | - | reports |
| Perl | - | text |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lunette
matrix 319        # the graph's slot IS the matrix layer
name Lunette
needs fs.read
needs fs.write
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

