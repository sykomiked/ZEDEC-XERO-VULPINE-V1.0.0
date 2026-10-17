# Criteria — gnomon

| | |
|---|---|
| app | `gnomon` |
| matrix layer | 107 |
| icon group | L1 |
| wave spec | `wave9/107-gnomon.md` |

## Purpose

exact shapes and numbers; saves in the Banker (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| F# | dotnet | exact numerics |
| Python | python | AI |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id gnomon
matrix 107        # the graph's slot IS the matrix layer
name Gnomon
needs fs.read
needs fs.write
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

