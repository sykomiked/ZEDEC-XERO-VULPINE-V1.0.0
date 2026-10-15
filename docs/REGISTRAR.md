# The Registrar: files as holdings in a verified store

The Registrar is the second layer of the Banker, ZXV's files app. The Banker stays
as it is: the same folders and files, the same view. The Registrar adds, under
and beside that view, the custody record every file gets. It treats each
store (the home folder, or a space) as a registry and each file as a
**holding**. Every holding has a verified content hash, an exact size
and a provenance record. Every operation on holdings behaves like a
transaction: it completes and verifies, or it leaves nothing behind.

It extends the files layer that the Banker (the current files app) already uses,
`kernel/arch/hosted/zxv_files.h`. That layer already guarantees:
- one root folder and no links out of it (F1–F3);
- whole-file writes through a staging file and a rename (F4);
- a Trash instead of deletion (F5);
- separate spaces (F7).

Status: design, not built. Section 6 gives the build order.

## 1. What the Registrar adds to the Banker

| The Registrar shows | Where it comes from | New? |
|---|---|---|
| Exact size in bytes, never rounded to MB | `zxf_entry_t.size` | Display only |
| Content hash (SHA-256) | Computed on admission and on audit, kept in the store's journal (the Tally) | New |
| Provenance: who or what changed it, when, from which source | The Tally (§2) | New |
| Four-valued state T/F/B/N | Audit results (§3) | New |
| Readers and writers right now: circuits, swarm peers, apps | The circuit runtime and zxvd sessions | New |
| Type and ports: EBCDIC record, COBOL source, ledger file… | Type detectors (copybook, `kernel/src/legacy`) | New |

A size is an integer number of bytes. It is shown exactly, with digit
grouping (4,194,304 bytes); a fraction such as "4194304/1" would add nothing.

## 2. The Tally: the store's journal

Each store (the home folder, or a space) has an append-only journal, the Tally. Each
entry is hash-chained to the one before, the same construction as `zxsh`'s
history and the lpres quarantine log. An entry records:
- the operation (admit, transfer, release, write, trash, audit, reconcile);
- the holdings involved, with their content hashes before and after;
- the actor: the user, an app, a circuit node or a swarm peer, with its
  identity key where it has one;
- the time (from `timesvc`).

`journal verify` checks the whole chain and every recorded hash against the
disk. A file changed behind the Registrar's back (another program, a disk
fault) does not match its last hash, and its state becomes FALSE (§3).

## 3. The four states of a holding

| State | Meaning | What the Registrar offers |
|---|---|---|
| TRUE | The content matches its last recorded hash (and every replica agrees) | Nothing to do |
| FALSE | The content does not match: changed outside the Registrar, or corrupt | Show the journal's last good hash; restore from a replica or the Trash if one matches |
| BOTH | Two sources disagree and both are recorded: two replicas, two versions written at once, a sync conflict | **Held.** The two versions sit side by side; reconcile opens a comparison or a circuit; nothing is overwritten |
| NEITHER | No record yet: the file was never admitted or audited | Admit it, which records its hash and provenance |

These are the same four values, with the same codes, as the rest of ZXV
(`kernel/src/lpres/lpres.h`), so a circuit or a `zxsh` script reads a
holding's state like any other verdict.

## 4. Operations as transactions

| Verb | Shell spellings (via the Rosetta Shell) | Effect |
|---|---|---|
| admit | `admit`, `cp` from outside the store | Copy in, hash, journal |
| release | `release`, `cp` to outside | Copy out; the holding stays |
| transfer | `mv`, `move`, `Move-Item` | Move within or between stores |
| audit | `audit` | Re-hash and compare; update states |
| reconcile | `reconcile` | Resolve a BOTH: pick, merge, or keep both under new names |

A multi-file operation (a 20 GB folder, say) runs in four steps:

1. **Stage.** Copy every file into a staging folder inside the destination
   store, hashing as it goes.
2. **Verify.** Read the staged files back and compare each one with its
   source hash.
3. **Commit.** Write one journal entry naming the whole set, then rename the
   staging folder into place.
4. **Clean up.** For a move, send the sources to the Trash, never delete
   them.

If anything fails before the commit entry, the staging folder is removed and
nothing visible has changed. If the machine stops after the commit entry but
before the rename, the next start finds the entry and completes the rename.
If it stops before the entry, the next start removes the stale staging folder.

One honest limit follows from the operating system: renaming a folder is
atomic only within one file system. Replacing an existing folder, or moving
between disks, uses the journal entry as the commit point instead. That is
the same guarantee, held by recovery on restart rather than by a single
system call.

## 5. With the canvas and the shell

- Dragging a holding onto the circuit canvas creates a source node. Its
  output port's type comes from the holding's detected type, so an EBCDIC
  record file offers records through its copybook, not raw bytes.
- A holding in state BOTH or FALSE can be a circuit input only as a
  four-valued value. A circuit cannot silently read corrupt or conflicting
  data as if it were good.
- In the Rosetta Shell, `admit`, `release`, `audit` and the usual
  `cp`/`mv`/`copy`/`Move-Item` all compile to the same transfer operations,
  journal entries included.

## 6. Build order

| # | Slice | Acceptance test |
|---|---|---|
| 1 | The Tally: hash-chained entries, `journal verify` | Tampering with any entry or any journaled file is detected |
| 2 | Admission and audit: SHA-256 per holding, the four states | A file edited outside the Registrar shows FALSE; a never-admitted file shows NEITHER |
| 3 | Multi-file transactions with staging, commit entry and restart recovery | Killing the process at each step leaves either the old state or the new one, never a mix (tested by fault injection at every step) |
| 4 | BOTH and reconcile: replicas and concurrent writes held side by side | Two conflicting writes leave both versions and a BOTH state, nothing lost |
| 5 | The Registrar layer in the Banker: a details panel (exact size, hash, provenance, state, current readers and writers) and state marks on the Banker's existing view, which otherwise stays as it is | The panel matches `journal verify` on the same store |
| 6 | Canvas source nodes with typed ports; shell verbs through the Rosetta Shell | Dragging a COBOL record file gives a record-typed port |

## 7. Limits

- The Registrar protects files it manages. A program writing directly to the
  disk bypasses it; the next audit detects the change (FALSE) but cannot
  prevent it.
- Hashing large files costs time. Admissions and audits of large stores run in
  the background and show progress; the states are "last audited" values
  with their time.
- The journal is not the money ledger. Holdings are not postings in
  `pay_ledger`, and moving a file never moves value.
