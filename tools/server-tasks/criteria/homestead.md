# Criteria — homestead

| | |
|---|---|
| app | `homestead` |
| matrix layer | 94 |
| icon group | L1 |
| wave spec | `wave8/094-homestead.md` |

## Purpose

the owner's home devices and scenes. No home-device method (devmesh is the owner's computers and phones): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| FORTRAN | trebago | exact numerics |
| IEC 61131-3 Structured Text | - | machine control |
| Zig | - | low-level system |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id homestead
matrix 94        # the graph's slot IS the matrix layer
name Homestead
needs net
needs peers
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

