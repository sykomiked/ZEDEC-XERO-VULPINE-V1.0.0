# Criteria — thicket

| | |
|---|---|
| app | `thicket` |
| matrix layer | 262 |
| icon group | L2 |
| wave spec | `wave21/262-thicket.md` |

## Purpose

Fronts community updates (kernel/src/peer_audit, kernel/src/evolve with evo_upd.c, evo_lineage.c and evo_trust.c, kernel/src/update). Signed update check (`update.check`), proposals and votes (`spaces.propose`, `spaces.vote`), the circle's discussion (`social.feed`, `social.post`), test runs (`play.run`), signatures (`web4.verify`), the trail (`journal.query`), versions (`backup.versions`). Listing forks and peer audits, submitting an audit and installing a reviewed update have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `sys.read`
- `journal`
- `backup`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | proposal ledger |
| FORTRAN | trebago | test score arithmetic |
| Ada | eve | refuses an update without an audit |
| APL | kron | review matrix |
| MUMPS | myra | fork tree |
| SAS | - | reports |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id thicket
matrix 262        # the graph's slot IS the matrix layer
name Thicket
needs net
needs peers
needs sys.read
needs journal
needs backup
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

