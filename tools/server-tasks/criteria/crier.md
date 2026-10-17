# Criteria — crier

| | |
|---|---|
| app | `crier` |
| matrix layer | 156 |
| icon group | L2 |
| wave spec | `wave12/156-crier.md` |

## Purpose

broadcasts one announcement to feeds (`social.post`), chat rooms (`chat.post`, `pspace.post`), radio (`harmonic.encode`, `ehop.bands`) and captions at once.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | notice register |
| FORTRAN | trebago | reach estimates |
| Ada | eve | rate limits |
| APL | kron | channel matrix |
| MUMPS | myra | audience tree |
| Gleam | - | messaging |
| Mojo | - | AI |
| Icon | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id crier
matrix 156        # the graph's slot IS the matrix layer
name Crier
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

