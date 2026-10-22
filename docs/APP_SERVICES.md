<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Services zxvd gives the apps

Several built-in apps declared things in their manifests that zxvd did not
yet serve. Coffer and Brazier said `needs notify`, but there was no method to
send a notice. Witness, the audit log, needed signed entries naming who
approved each one, and journal entries had no actor. Flint, the terminal,
could not run a program. Gatehouse had no way to manage any permission except
location. Signpost could not look up a name, Treadle had no timers and Warren
could not see or restart a process.

zxvd now has these methods. All of them answer on the control socket only and
never to a phone ([ZXVD.md](ZXVD.md#the-phone-link)).

| Method | Capability an app declares | For |
|---|---|---|
| `notify.send` | `notify` | a notice to the person |
| `journal.append` | `journal.write` | a signed audit entry with who approved it |
| `journal.query`, `journal.verify`, `log.tail` | `journal` | reading the history |
| `shell.run`, `shell.read`, `shell.write`, `shell.kill`, `shell.jobs` | `shell` | running programs and zxsh lines |
| `caps.list`, `caps.check`, `caps.grant`, `caps.revoke` | `caps` | the owner's answers on which app may use what |
| `dns.resolve`, `dns.reverse`, `dns.flush` | `net` | names and addresses |
| `did.parse`, `did.match` | none | reading a DID and checking a key against it |
| `schedule.add`, `schedule.list`, `schedule.cancel`, `schedule.fired` | `schedule` | timers |
| `proc.list` | `proc` | the machine's processes |
| `proc.signal`, `proc.restart` | `proc.control` | stopping and restarting them |

## Capabilities and the gate

An app lists its capabilities in its manifest, as in `needs notify`
(`kernel/src/apps/zxapp.h` R3). The table now also has `shell`,
`journal.write`, `caps`, `schedule`, `proc` and `proc.control`.

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
  `journal.write`, `caps` or `proc.control`.
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

## dns.* and did.*: Signpost

`dns.resolve` takes `name` and `family` (`any`, the default, `ipv4` or
`ipv6`). It asks the OS resolver, so the hosts file and the system's DNS
servers apply.

- A lookup can take seconds, so it runs on a thread beside zxvd's loop. The
  method answers at once with `state` `pending`; ask again (every 100 ms is
  plenty) until it says `done` with `addrs` or `failed` with `error`: `no
  such name`, `no answer now` (no server answered; try later) or `failed`.
- A name that is already an address is answered at once.
- Answers are kept 60 seconds and failures 15 (`ttl_ms` says how long is
  left). The OS resolver does not give record TTLs, so these are zxvd's own.
  `dns.flush` forgets them.
- A name is 1 to 253 bytes in labels of 1 to 63 letters, digits, `-` and
  `_`, with no `-` at either end of a label. One trailing dot is allowed.
  Anything else is refused before a lookup.
- At most 4 lookups run at once and 64 names are held. When every place
  holds a lookup still waiting, a new name gets -32026 `busy`.
- `dns.reverse` takes `addr` and gives the `name` the resolver has for it,
  in the same way.

`did.parse` reads a DID with no network:

- `did:key:z6Mk...` (an Ed25519 key) gives `key`, the 32 bytes in hex.
- `did:zxv:` and 52 lower-case base32 letters gives `id`, the 32-byte hash
  that `kernel/src/ident` makes from a key.
- Any other method, such as `did:web`, answers `supported: false`.
- A DID that does not decode exactly is refused (-32026): another key type
  in a did:key, a spare leading zero, or spare bits in a did:zxv.

`did.match` takes `did` and `key` (hex) and says whether the key belongs to
the DID: for did:key it is the same key, and for did:zxv its hash is the id.
For did:zxv, `key` is the key's encoding as cards and passkeys carry it.
Only the bytes are checked, not that they make a valid key.

Not here: finding a did:zxv's key from its id needs the peer directory,
which is the peer identity's records. did:web needs an HTTPS fetch.

## schedule.*: Treadle

A schedule is a time and a name an app registers. When it comes due, zxvd
records a firing and, if `notify` is true, posts the schedule's `note` as a
notice in the app's name. zxvd never runs anything for a schedule. Treadle
reads the firings and asks the owner before running a task.

`schedule.add` takes `app`, `name` (1-63 bytes), `note`? (up to 191),
`notify`? and one of:

- `at_ms`: once, at that time (ms since 1970, after now);
- `every_s`: every 60 seconds to 366 days, counted from `start_ms`? (default
  now);
- `daily`: `"HH:MM"` of the local day, on the `days`? given (`sun` to
  `sat`; default every day). Where a clock change skips the minute, it fires
  at the same distance past the change. Where a minute comes twice, it
  fires once.

It returns the schedule with its `id` and `next_ms`.

- `schedule.list` `app`?: the schedules, with `next_ms`, `last_ms` and
  `fired`.
- `schedule.cancel` `id`.
- `schedule.fired` `app`?, `after`?: the firings numbered after `after`, and
  `next`, the number to pass next time. Each has `due_ms`, `fired_ms`,
  `late` (more than a minute after it was due), `missed` (the times an
  `every` schedule skipped while zxvd was not running or the machine slept;
  it fires once for them) and `ended` (an `at` schedule is then gone).
  Firings are numbered from 1 and never reused. The last 256 are kept.

Limits: 128 schedules, 32 for one app. The schedules and the firings are
kept in `--schedule FILE` (with `--desktop`, `schedule.zxsd` beside the
books). It is mode 0600, rewritten whole and sealed with SHA-256, like the
capability answers. A damaged file is moved aside.

## proc.*: Warren

`proc.list` gives the processes of the account zxvd runs as (`all: true`
for every account's), at most 2048. Each has `pid`, `ppid`, `name`, `user`
and `uid`, `state`, `cpu_ms`, `rss` (resident bytes), `started_ms`, `sid`,
`mine` and `job`.

- `session_owner` is the account that owns the session's leader: who owns
  the session the process runs in.
- `job` is the `shell.*` job that started the process, else null.
- `restarts` lists the restarts under way or done, and `control` says
  whether `proc.signal` and `proc.restart` are on.

`proc.signal` takes `pid` and `signal`: `term` (the default), `kill`, `int`,
`hup`, `stop` or `cont`.

`proc.restart` takes `pid`:

- It reads the process's program, arguments and folder, then sends SIGTERM,
  and SIGKILL two seconds later if the process is still there.
- Once the process is gone, it starts the same program with the same
  arguments in the same folder, on its own: a new session with no terminal,
  so it outlives zxvd.
- It does not read another process's environment, so the new process has
  zxvd's.
- It answers at once with a `restart` id. Look in `proc.list`'s `restarts`
  for `started` with `new_pid`, or `failed` with `error`.

Both refuse (-32028):

- pid 0 or 1, zxvd itself and the process that started zxvd;
- another account's process, and one that is gone;
- anything while process control is off. It is off until zxvd is started
  with `--proc-control`, or with `--desktop`.

Linux and macOS; not on Windows yet.

Per-engine energy for Warren comes from the hardware drivers' `power.*`.

## Errors

| Code | Meaning |
|---|---|
| -32023 | a shell job refused: `shell.*` is off, there is no such job, the folder is bad, or all 16 places are running |
| -32024 | `journal.append`: no journal, no audit key, or the disk refused the entry |
| -32025 | a capability answer refused: no room, the file could not be written, or no answer to take back |
| -32026 | a name lookup or DID refused: `dns.*` is off, too many lookups are waiting, a DID does not decode, or its keys cannot be checked here |
| -32027 | a schedule refused: no room (or the app has 32), the file could not be written, or no such schedule |
| -32028 | a process refused: process control is off, a process zxvd does not act on, or one that is gone or cannot be read |

## Not here

- **A health method** (Wellspring). Wellspring keeps its log in the Hoard
  through `files.*` and needs no method of its own.
- **Power and thermal readings** (Brazier, Warren) come from the hardware
  drivers' `power.*` and `thermal.read`.

## Tests

`sh kernel/arch/hosted/test_zxvd_rpc.sh` covers all of these:

- the signed journal: links, a forged signature with a recomputed chain, a
  lost key, and no signer;
- notices: every field, collapsing, and refusals;
- capability answers: defaults, once, always, never, forget, location
  through `place`, the file and a damaged file;
- shell jobs: verdicts, argv, cwd, stdin, kill, a timeout that needs
  SIGKILL, the 256 KiB window, UTF-8 cut and replaced, 16 jobs, and close;
- names: addresses, localhost, a name that is not there, reverse, the cache,
  flush, and names refused; DIDs against vectors made outside zxvd;
- schedules on a test clock: each kind, missed and late firings, notices,
  limits, the file across runs and a damaged file;
- processes: listing, stop and go on, the refusals, a shell job's child, and
  restarts, one of a process that ignores SIGTERM.

`kernel/src/apps/test_zxapp.sh` checks the method-to-capability map and the
gate. `kernel/src/apps/test_apps.sh` checks that a refused method never
leaves the link.
