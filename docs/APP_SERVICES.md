<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Services zxvd gives the apps

Several built-in apps declared things in their manifests that zxvd did not
yet serve. Coffer and Brazier said `needs notify`, but there was no method to
send a notice. Witness, the audit log, needed signed entries naming who
approved each one, and journal entries had no actor. Flint, the terminal,
could not run a program. Gatehouse had no way to manage any permission except
location.

zxvd now has these methods. All of them answer on the control socket only and
never to a phone ([ZXVD.md](ZXVD.md#the-phone-link)).

| Method | Capability an app declares | For |
|---|---|---|
| `notify.send` | `notify` | a notice to the person |
| `journal.append` | `journal.write` | a signed audit entry with who approved it |
| `journal.query`, `journal.verify`, `log.tail` | `journal` | reading the history |
| `shell.run`, `shell.read`, `shell.write`, `shell.kill`, `shell.jobs` | `shell` | running programs and zxsh lines |
| `caps.list`, `caps.check`, `caps.grant`, `caps.revoke` | `caps` | the owner's answers on which app may use what |

## Capabilities and the gate

An app lists its capabilities in its manifest, as in `needs notify`
(`kernel/src/apps/zxapp.h` R3). The table now also has `shell`,
`journal.write` and `caps`.

`zxapp_method_cap` gives the capability each zxvd method needs. For example,
`log.tail` needs `journal`, `files.read` needs `fs.read` and `ledger.post`
needs `ledger.post`. A few methods need none: `rpc.discover`, `zxvd.version`
and `notes.list`.

An app's link to zxvd can carry a gate (`zxm_link_gate(link, zxapp_gate,
app)`). A method whose capability the manifest does not declare then fails
with `ZXM_E_DENIED` and is never sent. So an app without `needs journal`
cannot call `log.tail`.

The gate checks the manifest. The owner's own answers live in zxvd, under
`caps.*`.

## notify.send

`app`, `title` (1-63 bytes), `body` (up to 191), `action` (up to 47: what a
click opens), `level` (`low`, `normal` (the default), `high`, `urgent`),
`key` (up to 64). It returns `seq` and `stored`.

- A value that is too long is refused. It is never cut.
- The notice goes on the notification bus as kind `app`. It shows in
  `notes.list` and the window, and it reaches the OS notifier unless
  do-not-disturb holds it.
- Notices from the same app with the same `key` collapse into one unread
  notice with a count. Ten "new message" notices become one.
- `stored: false` means the bus had no room. Lesser notices never push out
  an urgent unread one.

## journal.append: signed audit entries

`actor` (1-32 letters, digits and `. _ - @ : +`), `text` (1-200 printable
bytes), `level`? It returns the entry: `seq`, `time_ms`, `level`, `source`
(`audit`), `text`, `actor`, `hash`, `prev`, `sig` and `key`.

- Every journal entry that `journal.query` returns now carries its own
  `hash` and the `prev` hash before it. Witness can check the chain without
  zxvd.
- An audit entry is signed with Ed25519 by the journal's audit key. The
  signature covers the entry's text, its actor, its sequence number and the
  hash of the line before it. So the signature also fixes the entry's place
  in the chain. The line's own hash then covers the signature.
- The key's seed is `audit.key` in the journal folder (mode 0600). It is
  made the first time an entry is signed. zxvd signs through libzxv's
  Ed25519, using a second capability that holds signing alone.
- `journal.verify` checks every signature as well as the chain, and returns
  `audit`, `sigs_checked` and `key`. A forged signature is a break, like a
  changed line, even when the chain hashes were recomputed to match.
- Only `journal.append` writes the source `audit`. Other zxvd events that use
  that name are written as `audit_`.

What the signature does and does not prove:

- zxvd records the actor it is given. The signature proves that this journal
  took the entry, in this place in the chain, under this name. It does not
  prove that the named person approved it. Witness should show the actor as
  "recorded by zxvd for".
- If `audit.key` is lost, `journal.verify` still checks the chain and says
  the signatures were not checked. A new key is made at the next entry, and
  the older entries then fail under it. Keep the journal folder backed up
  whole.

## shell.*: programs for the terminal

`shell.run` takes `line` (a zxsh line, run as `zxsh -c LINE`) or `argv` (one
program and its arguments, with no shell), and optionally `cwd` and
`timeout_ms`. It answers at once with `job` and `pid`. The job runs beside
zxvd's loop and never blocks it.

**Output**
- `shell.read` takes `job`, `from` (an offset) and `max`. It returns `out`,
  `from`, `next`, `dropped` and `done`.
- stdout and stderr share one pipe, so they keep the order the job wrote
  them in.
- The last 256 KiB are kept. `dropped` counts the bytes that went before the
  reader asked for them.
- A byte that is not UTF-8 becomes U+FFFD. A character cut by the end of a
  read waits for the next read.

**When a job ends**
- `shell.read` gives `exit` or `signal`, `verdict`, `timed_out` and
  `killed`.
- For a zxsh line, the verdict is zxsh's own: exit status 0 is true, 1 false,
  4 both, 5 neither and 3 forbidden (zxsh quarantined it).
- For a program, the verdict follows zxsh's rule for a process: 0 is true;
  1 to 125 false; 126 or 127 neither; a crash forbidden; any other signal
  neither.

**Input**
- `shell.write` sends `text` to stdin. `close: true` ends the input.
- There is no terminal (pty). Programs that need one see pipes.

**Limits**
- At most 16 jobs at once.
- A finished job stays until it is read to the end (`kept: false`) or killed.
  When every place is taken, the oldest finished job gives way to a new one.
- `timeout_ms` defaults to 10 minutes, and can be at most 24 hours.
- `shell.kill`, a timeout and stopping zxvd all send SIGTERM to the job's
  process group, then SIGKILL two seconds later.

**Turning it on**
- `shell.*` is off until zxvd is given a zxsh: `--zxsh PATH`, or with
  `--desktop`, a `zxsh` beside zxvd. Until then it answers -32023, so a
  stray `shell.run` from a test or a fuzzer starts nothing.
- A job runs as the owner, with the owner's permissions, like any terminal.

Not on Windows yet; there it answers -32023.

## caps.*: Gatehouse

For each app and capability, the owner can answer `always`, `once` (the next
use only) or `never`. With no answer, the default applies:

- An app may use a capability its manifest declares, unless the capability
  is **sensitive**: `camera`, `mic`, `location`, `shell`, `ledger.post`,
  `journal.write` or `caps`.
- A sensitive capability needs `always` or `once` first. An app that asks
  without an answer goes on the pending list and gets nothing until the
  owner answers.

The methods:

- `caps.check` `app`, `cap`: returns `allowed` and `state` (`allowed`,
  `pending` or `denied`). A `once` answer is used up by the check.
- `caps.grant` `app`, `cap`, `mode`? (`always` (default) or `once`).
- `caps.revoke` `app`, `cap`, `forget`?: sets `never`; with `forget` it takes
  the answer back so the default applies again.
- `caps.list` `app`?: every answer and pending ask, and the sensitive list.

Location has one answer per app, shared with Waymark. `caps.grant` and
`caps.revoke` set `place.permit`'s answer, `caps.check` asks through
`place.ask`, and `caps.list` shows `place`'s apps.

The answers file is `--caps FILE`. With `--desktop` it is `caps.zxcp`
beside the books. It is mode 0600, rewritten whole and sealed with SHA-256.
Only lasting answers (`always`, `never`) are written; `once` and pending asks
last for this run. A damaged file is moved aside as `.damaged`, and the
answers start empty.

## Errors

| Code | Meaning |
|---|---|
| -32023 | a shell job refused: `shell.*` is off, there is no such job, the folder is bad, or all 16 places are running |
| -32024 | `journal.append`: no journal, no audit key, or the disk refused the entry |
| -32025 | a capability answer refused: no room, the file could not be written, or no answer to take back |

## Not here yet

- **DNS and DID resolution** (Signpost). These are not built yet. A DNS
  lookup can block for seconds, so it has to run beside zxvd's loop, like
  the shell jobs do.
- **A health method** (Wellspring). Wellspring keeps its log in the Hoard
  through `files.*` and needs no method of its own.
- **Power and thermal readings** (Brazier) come from the hardware drivers'
  `power.*` and `thermal.read`.

## Tests

`sh kernel/arch/hosted/test_zxvd_rpc.sh` covers all of these:

- the signed journal: links, a forged signature with a recomputed chain, a
  lost key, and no signer;
- notices: every field, collapsing, and refusals;
- capability answers: defaults, once, always, never, forget, location
  through `place`, the file and a damaged file;
- shell jobs: verdicts, argv, cwd, stdin, kill, a timeout that needs
  SIGKILL, the 256 KiB window, UTF-8 cut and replaced, 16 jobs, and close.

`kernel/src/apps/test_zxapp.sh` checks the method-to-capability map and the
gate. `kernel/src/apps/test_apps.sh` checks that a refused method never
leaves the link.
