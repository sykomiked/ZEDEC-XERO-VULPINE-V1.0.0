# Criteria — cellar

| | |
|---|---|
| app | `cellar` |
| matrix layer | 19 |
| icon group | L1 |
| wave spec | `wave5/019-cellar.md` |

## Purpose

volumes, space by folder and space, Trash size, check and repair the Hoard, disc images (`sys.disk`, `files.*`).

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

| the entry's job (from the spec) | languages the spec names |
|---|---|
| used share from the used_share block (`use used_share 1`) | — |
| folder shares in kron + C++ wasm witnessed (copy the Banker's share pattern) | kron, c, wasm |
| Trash total exact in trebago | trebago |
| "safe to repair" limits in eve | eve |

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| Ada | eve | limits |
| C++ | cpp | exact numerics |
| FORTRAN | trebago | statistics |
| Julia | - | matrices and images |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cellar
matrix 19        # the graph's slot IS the matrix layer
name Cellar
needs sys.read
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

