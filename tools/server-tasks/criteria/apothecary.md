# Criteria — apothecary

| | |
|---|---|
| app | `apothecary` |
| matrix layer | 180 |
| icon group | L2 |
| wave spec | `wave14/180-apothecary.md` |

## Purpose

keeps clinical records and prescriptions in the Hoard (`files.read`, `files.write`) with signed approvals and medical ID. A clinician signs an approval with a passkey (the passkeys methods, under sys.read, added for this). Anything else missing: use only existing methods; list it in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `notify`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | billing codes |
| FORTRAN | trebago | dose calculation |
| Ada | eve | prescribing limits |
| APL | kron | lab result grid |
| MUMPS | myra | patient record tree |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |
| Tcl | - | scripting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id apothecary
matrix 180        # the graph's slot IS the matrix layer
name Apothecary
needs fs.read
needs fs.write
needs notify
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

