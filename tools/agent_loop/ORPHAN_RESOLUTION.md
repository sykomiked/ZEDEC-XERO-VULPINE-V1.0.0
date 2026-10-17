# Orphan resolution — the exhaustive criteria for the H100

The whole system must form one paraconsistent omni-tautology: every
module reachable, every export called, every layer bound to the layer
above and below, every claim testable from the bootloader to the browser
to the 407 apps. An orphan is a break in that tautology — a node with no
edge. This page is the standing instruction: **find every orphan, and
integrate it so the tautology closes.** Do not run it on a workstation;
run it on the build host, because the resolution is code, not advice.

## Step 1 — Find them (the scan)

```sh
python3 tools/agent_loop/orphan_scan.py --json /tmp/orphans.json
python3 tools/agent_loop/wire.py --check          # the circuit view
python3 tools/agent_loop/gap_audit.py --json /tmp/gaps.json
```

The scan groups every `kernel/**/*.c` by module directory, extracts every
non-static function definition, and checks whether **any** of them is
called from outside the module. Four orphan kinds come out:

| Kind | What it means |
|---|---|
| `defines no exported function` | the module directory is empty surface |
| `no export is called outside the module` | the module is self-contained dead weight |
| `dangling-use` (wire.py) | an app's `use <id>` names something not in the graph |
| `floating-app` (wire.py) | an app with no `needs` and no `use` |

## Step 2 — Classify each orphan (B or N, never guess)

For every orphan, decide by evidence, and record which:

- **It is genuinely dead.** The module was superseded; nothing should
  call it. → **Retire it**: delete the directory, remove its
  `build_system/strata.txt` line, and confirm `verify_layers.sh` still
  passes. Deleting dead surface *closes* the tautology.
- **It is genuinely needed and not yet called (a gap, state N).** A
  caller must exist and does not. → **Integrate it** (Step 3).
- **The scan is wrong (state B).** The "orphan" is called through a
  function pointer, a table, a syscall dispatch, or an assembly entry the
  scan cannot see. → **Teach the scan**: add the caller to the module's
  registry (see Step 4) so the edge is visible. Never silence a real
  orphan by calling it a false positive without naming the dispatch path.

A conflict between two sources (the scan says orphan, the graph says
used) is state **B**: name both readings and resolve by the running
evidence (the boot log, the test verdict), never by picking one silently.

## Step 3 — Integrate (the recipe, by kind)

Every integration is **complete code**: no stub, no `TODO`, no
placeholder branch. If it cannot be finished, it is not committed.

**A module with a real consumer.** Wire it at the consumer's natural
seam, exactly as the ai_layer was:
1. Add the include where the consumer lives.
2. Call its init once, at the point its preconditions are met (after the
   disc's models are verified, after the porter house is up, after the
   registry is loaded — whatever its header names).
3. Call its periodic entry in the idle loop (the timeouts, the pump, the
   drain) so it is *driven*, not merely started.
4. Build the kernel and confirm zero errors.

**A module that is a service with no single consumer.** Give it a
capability in `kernel/src/apps/zxapp.c`'s `CAPS[]`, provide it through
the module, and let the apps that need it declare `needs <cap>`. The
edge then exists in the graph and the apps' manifests.

**A module that is a substrate others must build on.** Move the shared
declaration *down* a stratum and include it from the dependants; never
reach *up*. `build_system/verify_layers.sh` must pass.

**A dangling `use`.** Either the named app exists under another id
(correct the `use`), or it is genuinely missing (add it to the wave spec
with its brief, then build it), or it names a *block* rather than an app
(resolve it to its owning app, or give blocks their own edge kind in the
graph). A `use` that points at nothing refuses the manifest — it must
resolve.

**A floating app.** Every app declares `needs` (what it consumes from
the kernel) and, where it calls another app, `use`. An app with neither
is a leaf that nothing reaches; give it its real dependencies.

## Step 4 — Teach the scan (so the tautology is visible)

When an edge exists only through a dispatch the scan cannot see, record
it where the scan reads it, so the graph is complete:

- **function-pointer tables** — name the table and the module that
  dispatches through it in `build_system/edges.txt` (create it if
  absent), one line per edge: `dispatcher -> symbol # the table's name`.
- **syscall dispatch** — the syscall table's entries are edges from the
  syscall layer to each handler; the scan must read the table.
- **assembly entries** — the entry points (the boot stubs) are edges from
  the linker script; record them.

The goal of this step is that a *second* scan run finds the module
connected — the same discipline as "prove the test can fail": prove the
orphan can be seen.

## Step 5 — The omni-tautology check (the closing criterion)

The system is an omni-tautology when, for every node, the edge exists in
both directions:

1. **Downward**: every app's `needs` is provided by a module that
   provides it (`gap_audit.py`: zero `unregistered-need`).
2. **Upward**: every module's export is called by something above it
   (`orphan_scan.py`: zero orphans, after Step 4's honest accounting).
3. **Sideways**: every `use` resolves to an app that exists
   (`wire.py`: zero `dangling-use`).
4. **The paraconsistent closure**: where two witnesses of the same law
   disagree, the join is **BOTH** and a check expects it — proven on the
   node, exactly as `count_ok`'s Eve/Kron pair. The tautology is
   *paraconsistent*: it holds even where the parts contradict, because
   BOTH is a value, not an error.
5. **The layer law**: `build_system/verify_layers.sh` prints PASS.

When all five hold, the tautology is closed: every pin driven, every net
terminated, every claim proven from the boot to the app.

## Step 6 — The test code for the resolution itself

Every orphan resolution gets a test that **fails before the fix and
passes after** (the repository's rule: prove the test can fail):

- For an **integration**, the test asserts the module's init ran (a
  banner symbol, a counter, a state the consumer reads) — break the
  call, watch the test fail, restore.
- For a **retirement**, the test asserts the symbol is absent from the
  link map (`llvm-nm`), so a stray include is caught.
- For a **taught edge**, the test asserts the scan reports the module
  connected, so the dispatch stays visible.

## The loop

```
while orphans remain:
    run the scan (orphan_scan + wire + gap_audit)
    classify each orphan: dead | gap | misread
    retire the dead, integrate the gaps, teach the scan the misreads
    add the resolution test (prove it fails first)
    build the kernel, run verify_layers, run verify-all
    re-scan: the orphan count must strictly fall
    commit only when every check passes
```

The target is a single number: **zero orphans**, with every resolution
tested and every check green — the paraconsistent omni-tautology closed
from the bootloader to the 407 apps. Run it on the build host: the
resolution is code, and the machine that compiles it is the machine that
proves it.
