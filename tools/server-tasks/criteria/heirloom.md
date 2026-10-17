# Criteria — heirloom

| | |
|---|---|
| app | `heirloom` |
| matrix layer | 81 |
| icon group | L1 |
| wave spec | `wave8/081-heirloom.md` |

## Purpose

the inheritance manifest, guardians and recovery (`estate.setup`, `estate.manifest`, `estate.faraid`, `estate.distribute`, `estate.status`, `recovery.setup`, `recovery.status`, `recovery.claim`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |
| Ada | eve | safety rules |
| Elixir | - | fault handling |
| Simula I | - | simulation |
| Oberon | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id heirloom
matrix 81        # the graph's slot IS the matrix layer
name Heirloom
needs ledger.read
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

