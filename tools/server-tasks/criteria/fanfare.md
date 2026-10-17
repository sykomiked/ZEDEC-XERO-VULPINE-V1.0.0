# Criteria — fanfare

| | |
|---|---|
| app | `fanfare` |
| matrix layer | 364 |
| icon group | L3 |
| wave spec | `wave30/364-fanfare.md` |

## Purpose

Recognises contributions in a team or community with badges, credits and an auditable reputation: the circle (`social.feed`, `social.post`), votes (`spaces.propose`, `spaces.vote`), credits in the books (`ledger.post`, `ledger.hold`, `ledger.holds`, `ledger.release`), announcements (`chat.post`, `pspace.post`).

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `ledger.read`
- `ledger.post`
- `swarm`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | credit ledger |
| FORTRAN | trebago | reputation scoring |
| Ada | eve | anti-gaming limits |
| APL | kron | contribution matrix |
| MUMPS | myra | member tree |
| PHP | php | reports |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id fanfare
matrix 364        # the graph's slot IS the matrix layer
name Fanfare
needs peers
needs net
needs ledger.read
needs ledger.post
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

