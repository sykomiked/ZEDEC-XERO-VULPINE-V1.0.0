# Criteria — grotto

| | |
|---|---|
| app | `grotto` |
| matrix layer | 263 |
| icon group | L2 |
| wave spec | `wave21/263-grotto.md` |

## Purpose

Keeps a hidden, encrypted compartment for the most sensitive files: a private space (`spaces.create`, `spaces.get`), files moved in only through `files.cross` and kept with `files.*` (`files.list`, `files.read`, `files.write`), unlock by passkey (`passkeys.auth_begin`, `passkeys.auth_finish`), the seat locked on leaving (`seats.lock`). Encrypting a compartment at rest has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | access records |
| FORTRAN | trebago | key derivation |
| Ada | eve | unlock limits |
| APL | kron | block map |
| MUMPS | myra | folder tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id grotto
matrix 263        # the graph's slot IS the matrix layer
name Grotto
needs fs.read
needs fs.write
needs sys.read
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

