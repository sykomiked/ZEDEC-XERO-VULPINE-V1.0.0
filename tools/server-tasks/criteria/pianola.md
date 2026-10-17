# Criteria — pianola

| | |
|---|---|
| app | `pianola` |
| matrix layer | 354 |
| icon group | L3 |
| wave spec | `wave29/354-pianola.md` |

## Purpose

Builds, tests and ships apps in all five pillars from one workspace: scripts and runs (`play.catalogue`, `play.run`, `play.draft`), sources in the Hoard (`files.read`, `files.write`, `search.query`), releases by CID (`aipi.put`, `web4.cid`), versions (`backup.versions`). Build runs and signing have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `sys.read`
- `ai.ask`
- `backup`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | release billing |
| FORTRAN | trebago | numeric kernels |
| Ada | eve | contracts and limits |
| APL | kron | test arrays |
| MUMPS | myra | module tree |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id pianola
matrix 354        # the graph's slot IS the matrix layer
name Pianola
needs fs.read
needs fs.write
needs search
needs sys.read
needs ai.ask
needs backup
needs net
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

