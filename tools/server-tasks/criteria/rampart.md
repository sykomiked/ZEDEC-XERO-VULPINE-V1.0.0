# Criteria — rampart

| | |
|---|---|
| app | `rampart` |
| matrix layer | 289 |
| icon group | L3 |
| wave spec | `wave24/289-rampart.md` |

## Purpose

a security operations centre for an organisation: alerts, incidents, responders and signed after-action reports (`journal.query`, `journal.verify`, `log.tail`, `power.top`, `files.write`).

## Capabilities (needs — the exact registry names)

- `journal`
- `sys.read`
- `notify`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | incident costs |
| FORTRAN | trebago | risk scoring |
| Ada | eve | response playbook limits |
| APL | kron | alert correlation grid |
| MUMPS | myra | org asset hierarchy |
| RPG | - | reports |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id rampart
matrix 289        # the graph's slot IS the matrix layer
name Rampart
needs journal
needs sys.read
needs notify
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

