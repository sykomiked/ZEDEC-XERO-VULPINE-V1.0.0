# Criteria — dragoman

| | |
|---|---|
| app | `dragoman` |
| matrix layer | 244 |
| icon group | L2 |
| wave spec | `wave20/244-dragoman.md` |

## Purpose

Fronts the language layer (kernel/src/lingua with catalogue.tsv, the 104 languages; kernel/src/legacy (cobol.c, fortran.c); kernel/src/sutra; kernel/src/apps/LANGUAGE_FIT.tsv and `zxapp_lang_of` in kernel/src/apps/zxapp.c for the twin names). Runs each runtime's samples (`play.catalogue`, `play.run` on scratch state, `play.draft` untrusted), lists the daemon (`rpc.discover`, `zxvd.version`), checks for runtime updates (`update.check`), reads files (`files.read`). Installing a runtime has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `net`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | COBOL host |
| FORTRAN | trebago | Fortran host |
| Ada | eve | Ada host and runtime limits |
| APL | kron | APL and K host |
| MUMPS | myra | MUMPS host |
| OCaml | - | parsing |
| Mojo | - | AI |
| Crystal | - | low-level system |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id dragoman
matrix 244        # the graph's slot IS the matrix layer
name Dragoman
needs sys.read
needs fs.read
needs net
needs ai.ask
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

