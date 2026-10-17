# Criteria — windbreak

| | |
|---|---|
| app | `windbreak` |
| matrix layer | 222 |
| icon group | L2 |
| wave spec | `wave18/222-windbreak.md` |

## Purpose

Fronts the Pit Boss and the Cage (docs/PIT_BOSS.md, docs/CAGE.md; admission in kernel/src/immigration, capabilities in kernel/src/capctl, slot, layer, budget and needs in kernel/src/apps/zxapp.c via `zxapp_slot_layer` and `zxapp_needs`, scheduling in kernel/src/sched). Shows consumers and their energy (`power.top`, `power.status`), the machine (`sys.info`), engines and switches (`features.get`), agents and budgets (`swarm.state`), events and rule actions (`log.tail`, `journal.query`). A running-program table, the Cage's cleared/held/refused queue and the rulebook have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `journal`
- `swarm`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | rule-firing log |
| FORTRAN | trebago | tick and memory budgets |
| Ada | eve | crossing rules between L1, L2 and L3 |
| APL | kron | process table |
| MUMPS | myra | quarantine records |
| Prolog | - | rules |
| Perl | - | text |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id windbreak
matrix 222        # the graph's slot IS the matrix layer
name Windbreak
needs sys.read
needs journal
needs swarm
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

