# Criteria — satchel

| | |
|---|---|
| app | `satchel` |
| matrix layer | 7 |
| icon group | L1 |
| wave spec | `wave3/007-satchel.md` |

## Purpose

build, sign and install ZXV packages (`zxpkg`, AIPI signed bundles).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

| the entry's job (from the spec) | languages the spec names |
|---|---|
| package size and file-count limits in eve | eve |
| total size exact in trebago | trebago |
| per-file share of the package in kron + Rust wasm witnessed | kron, rust, wasm |
| package record in myra | myra |

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| APL | kron | arrays |
| FORTRAN | trebago | exact numerics |
| Rust | rust | low-level system |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id satchel
matrix 7        # the graph's slot IS the matrix layer
name Satchel
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

