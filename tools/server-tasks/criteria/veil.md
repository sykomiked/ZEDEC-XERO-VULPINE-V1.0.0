# Criteria — veil

| | |
|---|---|
| app | `veil` |
| matrix layer | 365 |
| icon group | L3 |
| wave spec | `wave30/365-veil.md` |

## Purpose

Hides what you choose from apps and people: shade data, set blackout periods and see who tried to look. Permissions and the perimeter (`features.get`, `sys.net`, `passkeys.list`), place sharing (`place.permit`, `place.apps`, `place.forget_here`), attempts on the record (`journal.query`). A per-app data-shading method has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `net`
- `peers`
- `location`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | data-access fees |
| FORTRAN | trebago | anonymity metrics |
| Ada | eve | disclosure limits |
| APL | kron | access grid |
| MUMPS | myra | identity tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id veil
matrix 365        # the graph's slot IS the matrix layer
name Veil
needs sys.read
needs net
needs peers
needs location
needs journal
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

