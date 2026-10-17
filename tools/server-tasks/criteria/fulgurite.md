# Criteria — fulgurite

| | |
|---|---|
| app | `fulgurite` |
| matrix layer | 245 |
| icon group | L2 |
| wave spec | `wave20/245-fulgurite.md` |

## Purpose

ZXV builds ZXV. Fronts build_system/mk_golden_disc.sh, build_system/golden.contents, build_system/golden.models, build_system/test_golden_disc.sh, kernel/src/zxpkg (zxrelease) and kernel/src/provenance. Runs build and test templates (`play.run`), reads sources and writes artefacts and the bill of materials (`files.read`, `files.write`), versions (`backup.versions`), room on disk (`sys.disk`), publishes the bill of materials by CID (`aipi.put`, `web4.cid`), checks for updates (`update.check`). A build-run method and image signing have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `backup`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | release ledger |
| FORTRAN | trebago | build timing |
| Ada | eve | refuses an unpinned download |
| APL | kron | reproducibility diff grid |
| MUMPS | myra | bill-of-materials tree |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id fulgurite
matrix 245        # the graph's slot IS the matrix layer
name Fulgurite
needs fs.read
needs fs.write
needs sys.read
needs backup
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

