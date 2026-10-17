# Criteria — tributary

| | |
|---|---|
| app | `tributary` |
| matrix layer | 225 |
| icon group | L2 |
| wave spec | `wave18/225-tributary.md` |

## Purpose

Asks several models the same question and joins their answers by witnessed agreement: local models (`model.info`, `swarm.ask`, `swarm.state`), another node's model (`peer.ask`, priced, owner-approved), energy per answer (`power.status`), scored templates (`play.run`).

## Capabilities (needs — the exact registry names)

- `ai.ask`
- `swarm`
- `peers`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | query ledger |
| FORTRAN | trebago | confidence scoring |
| Ada | eve | budget caps |
| APL | kron | answer matrix |
| MUMPS | myra | source tree |
| Mojo | - | AI |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tributary
matrix 225        # the graph's slot IS the matrix layer
name Tributary
needs ai.ask
needs swarm
needs peers
needs sys.read
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

