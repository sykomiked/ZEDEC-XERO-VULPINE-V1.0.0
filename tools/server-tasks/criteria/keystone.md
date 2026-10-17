# Criteria — keystone

| | |
|---|---|
| app | `keystone` |
| matrix layer | 4 |
| icon group | L1 |
| wave spec | `wave3/004-keystone.md` |

## Purpose

displays, sound, keyboard, language, accessibility, network, sharing, the Hoard, model choice, budgets (docs/SYSTEM_UTILITIES.md).

## Capabilities (needs — the exact registry names)

- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

| the entry's job (from the spec) | languages the spec names |
|---|---|
| setting limits (brightness, volume, text size, budgets) in eve + kron witnessed | eve, kron |
| budget shares across apps in kron + trebago | kron, trebago |

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| APL | kron | arrays |
| FORTRAN | trebago | exact numerics |
| COBOL | sutra | money and ledgers |
| C++ | cpp | money and ledgers |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id keystone
matrix 4        # the graph's slot IS the matrix layer
name Keystone
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

