# Rosetta Shell: one shell for every command dialect

The Rosetta Shell accepts a command written for POSIX shells, PowerShell or
cmd.exe. It translates the command into one typed intermediate form, the
**CLI-IR**, and runs that form on ZXV's own file and process layer. It is
built on `zxsh` (`tools/zxsh`), which already gives every command a
four-valued verdict and holds a contradiction until a person resolves it.

Status: design, not built. Section 6 gives the build order.

## 1. The pipeline

```
input line
  -> each dialect's grammar tries it (POSIX, PowerShell, cmd.exe)   deterministic
  -> each success yields a CLI-IR term                              deterministic
  -> the four-valued verdict over the terms decides what happens    deterministic
  -> the IR runs on ZXV's file and process layer                    deterministic
  (a model is consulted only when no grammar accepts the line, and
   only to propose; see section 4)
```

Dialect detection is not a guess. Each dialect is a grammar. A line either
parses in it or it does not, and a parse produces an IR term or nothing.

## 2. The CLI-IR

A small, typed set of operations with explicit arguments. Examples:

| CLI-IR | POSIX | PowerShell | cmd.exe |
|---|---|---|---|
| `fs.list(path=".", recurse=true)` | `ls -R` | `Get-ChildItem -Recurse` | `dir /s` |
| `fs.copy(src, dst)` | `cp a b` | `Copy-Item a b` | `copy a b` |
| `text.search(pattern, files, ignore_case=true)` | `grep -i p f` | `Select-String -Pattern p f` | `findstr /i p f` |
| `fs.find(root=".", name="*.txt")` | `find . -name "*.txt"` | `Get-ChildItem -Recurse -Filter *.txt` | `dir /s /b *.txt` |
| `text.find_in_file(pattern, file)` | `grep -F p f` | `Select-String -SimpleMatch p f` | `find "p" f` |
| `proc.signal(pid, TERM)` | `kill 1042` | — | — |
| `proc.stop(pid, force=true)` | `kill -9 1042` | `Stop-Process -Id 1042` (alias `kill`) | `taskkill /f /pid 1042` |

Every IR operation states its effect class: read, write, destroy or process
control. Each effect class maps to a `zxsh` capability (`cap`), so `cap drop
write` holds in every dialect at once.

## 3. Ambiguity is a verdict, not a guess

Each grammar gives one of three results: a term, no parse, or a parse
error. The shell combines them on the Belnap lattice that `zxsh` already
uses:

| Grammars that accept | Their terms | Verdict | What happens |
|---|---|---|---|
| exactly one | — | TRUE | runs |
| several | identical IR | TRUE | runs (`cp a b` and `copy a b` mean the same) |
| several | different IR | BOTH | **held**: the shell shows each reading and its effect; Enter runs the default reading, Tab switches |
| none | — | NEITHER | offers the model's proposal (§4), or says why each grammar refused |

The two examples from the brief resolve this way:

- **`find . -name "*.txt"`** parses only in POSIX: cmd.exe's `find` has no
  `-name` switch. It runs with no hold.
- **`find "hello" sample.txt`** parses in both. cmd.exe reads it as "search
  sample.txt for the string hello". POSIX reads both words as starting
  paths. The shell then checks a fact instead of guessing. When no path
  named `hello` exists, the POSIX reading can only fail, so it drops out and
  the cmd.exe reading runs. When such a path does exist, both readings are
  live, and the line is held as BOTH.
- **`kill 1042`** parses in POSIX (SIGTERM, which the program may catch) and
  in PowerShell (`kill` is an alias of `Stop-Process`, which ends the
  process at once). The IR differs, so the verdict is BOTH and the line is
  held with both readings shown.

A held line uses `zxsh`'s existing hold mechanism (`held`, `resolve`,
`drop`). A script never resolves a hold: scripts run without the resolve
capability, so an ambiguous line in a script stops there.

## 4. Where a model helps, and where it does not

The model has two jobs. Both produce proposals, and nothing it writes runs
until the user confirms.

- **Plain-language fallback (NEITHER).** "Find all files modified yesterday
  over 50 MB and delete them" goes to the local model with the CLI-IR
  grammar as its decoding mask (`zt_gram`). It can only answer with a
  well-formed IR term. The shell shows the term and its native spelling in
  the user's usual dialect, plus a dry run (the exact list of files it would
  touch). An IR term whose effect class is *destroy* needs the user to type
  the confirmation word, not just press Enter.
- **Flag repair.** `ls -Recurse` (a PowerShell flag on a POSIX command)
  fails the POSIX grammar. The shell proposes `ls -R` and highlights the
  change. It does not rewrite silently: a silent fix can be wrong in a
  destructive direction (a misread flag on `rm` must never run).

Most repairs need no model at all. A table of known cross-dialect flag
equivalents (`-Recurse`/`-R`/`/s`) proposes the fix deterministically. The
model is consulted only when the table has nothing.

## 5. Typed pipes

Pipes carry **records** by default: a listing is rows of name, size, time
and kind, not text.

- A record-aware stage filters fields: `| where Size -gt 1000` and its
  POSIX-style equivalent compile to the same `rec.filter(Size > 1000)`.
- A text tool (`| grep test`, an external program) receives the records
  serialised as tab-separated lines with a header, in a fixed order, so the
  bytes are the same on every machine.
- Going back from text to records needs a declared format (`| from tsv`,
  `| from json`, a copybook). The shell never guesses columns from text.

Listings are sorted bytewise by name, and sizes are exact byte counts, so
the same command over the same tree prints the same bytes anywhere.

## 6. Build order

| # | Slice | Acceptance test |
|---|---|---|
| 1 | CLI-IR types, effect classes and their capability mapping, inside `zxsh_core` | Every IR op round-trips through its text form; `cap drop write` refuses every write op |
| 2 | POSIX front-end for the IR subset; the rest still runs as external programs, as today | The table in §2 parses to the stated IR |
| 3 | PowerShell and cmd.exe front-ends for the same subset | Each row of §2 gives identical IR in all three dialects |
| 4 | The verdict over parses, and holds with readings shown | `kill 1042` is held with two readings; `cp a b` runs |
| 5 | Record pipes and the fixed TSV serialisation | Byte-identical output across x86_64 and arm64 |
| 6 | Flag-equivalence table and repair proposals | `ls -Recurse` proposes `ls -R`; nothing runs unconfirmed |
| 7 | Model fallback under the IR grammar (needs `docs/AI_MODEL.md` slice 3) | A plain-language request yields a shown, dry-run IR term; *destroy* needs the typed word |
| 8 | Each run IR command becomes a circuit node on the canvas, from `zxsh`'s hash-chained history | A session replays as a circuit that gives the same output |

## 7. Limits

- It runs the IR subset natively. It does not emulate Bash or PowerShell.
  A line outside the subset runs as an external program in its own dialect,
  only where that program exists, with `zxsh`'s verdicts and holds around
  it.
- Capabilities are checks inside the shell. An external program it starts
  keeps the user's OS permissions (as `zxsh`'s README already states).
- Exact dialect quirks (globbing rules, quoting, PowerShell's object
  formatting) are reproduced only as far as the IR subset needs.
