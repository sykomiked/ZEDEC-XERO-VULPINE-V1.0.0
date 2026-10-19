<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# zxsh: the paraconsistent shell

In zxsh every command returns a **verdict** instead of a bare exit code: true,
false, **both** (a contradiction) or **neither** (no answer). Each verdict also
carries how its evidence is trending and a lifecycle phase. Together these
form the 64-state hexagram tag from `kernel/src/lpres/lpres_hex.h`, and what
runs next is always that tag's entry in the kernel's action table.

```
make            # build/zxsh  (use CC=gcc-11 to match CI)
make test       # core, sub-terminal and end-to-end tests, ASan/UBSan
build/zxsh                  # interactive
build/zxsh -c 'cmd'         # one command line
build/zxsh script.zxs a b   # a script; $1 $2 ... ($arg1 $arg2 also work)
```

## If you know sh or bash, you already know zxsh

Commands, pipes and redirects are the ones you use every day, and every
program on your PATH (git, make, cc, python, npm, cargo, docker, ...) runs
unchanged. Type `help` for the full list, or `help NAME` for one command.

- `;` `&&` `||` `|` `<` `>` `>>` `2>` `2>>` `2>&1` `>&2` `&>` `&>>`
- `cmd &`, `jobs`, `wait [%N]`
- `$VAR` `${VAR}` (the environment included), `$(...)`, backticks,
  `$((1 + 2))`, `$1`..`$9`, `$#`, `$@`, `$?` (the numeric exit status)
- `NAME=value cmd`, `export`, `unset`, `alias`, `unalias`, `source FILE`, `. FILE`, `type`
- `if` / `for` / `while` / `until`, `break`, `continue`, `cd -`, `~`
- Line editing: arrows, Home/End, Ctrl-A/E/K/U/W/Y, Up/Down history,
  Ctrl-R search, Tab completes commands and files (Tab twice lists them).

A line zxsh does not read itself (`case`, functions, `[[ ]]`, `( )` groups)
goes to bash, and zxsh says so on stderr. Its exit status becomes the
verdict. `ZXSH_COMPAT=0` turns this off.

## Sub-terminals

One terminal can hold several, all running at the same time:

| You write | What happens |
|---|---|
| `split` / `split -v` | a new shell beside this one (or below it) |
| `split make test` | a sub-terminal running that command |
| `tab` | a new shell in a new tab |
| `together 'make' 'make test' 'npm run lint'` | each command in its own sub-terminal at once; the verdict is true or false when they all agree, and **both** when they disagree |

Each sub-terminal has a title bar with its own verdict letter (T, F, B, N,
`!`), and each tab shows its combined letter. So `together 'make test'
'make lint' && git push` holds the push when the checks disagree, until you
look and `resolve` it.

The keys are the familiar multiplexer ones: **Ctrl-B** then `%` split right,
`"` split below, arrows or `o` move, `c` new tab, `n`/`p` next or previous
tab, `1`-`9` jump to a tab, `x` close, `q` leave, `?` the key list. Without
a terminal (CI, a pipe), `together` still runs its commands at the same time
and labels each output line `[1]`, `[2]`, ...

## What zxsh adds

| You write | What happens |
|---|---|
| `a && b` | `b` runs only if `a`'s action is *proceed* |
| `a \|\| b` | `b` runs only if `a` is *refused* (false) |
| `a \| b \| c` | the verdict combines **every** stage, so a failing producer is not hidden |
| `a <?> b` | runs both and compares them: agreement is true, disagreement is *both* |
| `! a` | negation (both and neither stay as they are) |
| `if c; then A; else B; both C; neither D; fi` | one branch for each of the four answers |
| `for x in ...; do ...; done` | the loop's verdict combines its iterations |
| `held` / `resolve 3 true` / `drop 3` | a both or neither result **holds** the rest of the line until a person decides |
| `why` | explains the last verdict and its action |
| `cap`, `cap drop write`, `sandbox exec -- cmd` | session capabilities, which can only narrow |
| `history`, `history verify`, `qlog`, `qlog verify` | hash-chained history and quarantine files |
| `zxv METHOD {json}` | calls zxvd over its socket (`$ZXVD_SOCK` overrides the path) |

**How a process's end becomes a verdict.**
- Exit 0 is true, and 1 to 125 is false.
- 126 or 127 (the program could not run) is neither.
- A crash (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS, SIGTRAP) is
  **forbidden**. The command is quarantined and the rest of the input stops.
- Any other signal is neither/entropic: the program was interrupted.
- A program can report its own verdict by writing `both`, `true/negentropic`,
  `false/forbidden` and so on to fd 3 (`$ZXSH_VERDICT_FD`). It cannot hide a
  crash this way.

**Honest limits.**
- Capabilities are checks inside the shell. A program it runs keeps the
  user's OS permissions.
- Only a person at an interactive prompt can resolve a hold. Scripts and
  background jobs run without the resolve capability.
- A hold resolved later sees the variables as they are at that moment, not
  as they were when it was held.
- Inside an `if`'s clauses, `both` and `neither` are keywords, so write
  `verdict both` there.
- Background jobs cannot be brought to the foreground (no `fg`, `bg` or
  Ctrl-Z). Aliases apply to typed lines only, as in bash scripts.
- A while/until whose condition is both or neither stops and reports it.
- Sub-terminals treat every character as one column wide, so wide
  characters (many emoji, CJK) can misalign.
- The Windows host (CreateProcess) and the bare-metal console are next. The
  core (`zxsh_core.c`) has no allocation and no stdio, so pterm can embed it.
