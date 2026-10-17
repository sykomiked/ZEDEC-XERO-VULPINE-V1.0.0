# The gap-filling criteria — 100% vertical integration, exhaustively spelled out

The H100's standing instruction: while the apps build, audit the whole
stack for gaps and fill every one in a quality fashion. A gap is anything
that breaks the vertical chain: a layer that cannot reach the layer below
it, a capability nothing provides, an app that cannot load, a module
nothing calls, a claim with no test. `tools/agent_loop/gap_audit.py`
finds them; this page says exactly how to fill each.

Run the detector first:

```sh
python3 tools/agent_loop/gap_audit.py --json /tmp/gaps.json
```

Every gap it prints is one of the kinds below. Fill each by its recipe.
Do not close a gap by weakening a check, deleting a test, or relaxing a
layer rule — the repository's own rule is that a change is done when its
checks pass, and a gap closed by lowering the bar is still a gap.

## 1. `apps/unbuilt` — an app with no working manifest

**What it is.** The graph lists the app; no `.zxapp` loads and passes.

**Fill it.** Read `tools/server-tasks/exhaustive/<app>.md` — it carries
the purpose, the capability names, the dependencies, the language
bindings, the complete manifest where the pattern library reaches, and
the spelled-out slots where it does not. Emit the manifest:

```
zxapp 1
id <app>
matrix <the layer from the brief, 0..144>
name <the display name>
needs <each exact capability name>
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<the source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

**The acceptance test.** The node loads every logic entry and every check
passes: `curl -X POST --data-binary @<app>.zxapp $ZXVD_URL/api/app` returns
`loaded, N checks passed`. Anything else is not done.

## 2. `graph/dangling-use` — a `use` naming an app that does not exist

**What it is.** App A's manifest calls `use <id> <layer>` where `<id>` is
not in the graph.

**Fill it.** Either the app exists under another id — correct the use
line to the real id; or it is genuinely missing — add it to the wave spec
with its own brief, then build it (recipe 1). Never leave a use line
pointing at nothing: the loader refuses the manifest.

## 3. `capabilities/unregistered-need` — an app needs a name the kernel lacks

**What it is.** An app's `needs <name>` is not in the kernel's `CAPS[]`
table (`kernel/src/apps/zxapp.c`).

**Fill it.** Two honest options: (a) the capability is real and missing —
add the exact string to `CAPS[]` and implement its provider module, with
a test proving the provider answers; or (b) the app's needs line is wrong
— correct it to a registered name. A typo'd capability refuses the whole
manifest, so this gap blocks the app entirely.

## 4. `capabilities/unused-capability` — a registered name no app needs

**What it is.** `CAPS[]` lists a capability no app requires.

**Fill it.** Either an app should use it (add the `needs` line where it
belongs and use the capability in that app's logic), or the capability is
retired (remove it from `CAPS[]` and from its provider). An unused
capability is dead surface; the design claims 100% integration, so it is
either used or gone.

## 5. `strata/unmapped-sources` — a source with no stratum

**What it is.** A `.c`/`.h` in the kernel is not in `build_system/strata.txt`.

**Fill it.** Assign the correct stratum (S0 base through S8 products) by
what the module *is*, not where it sits. Then run
`build_system/verify_layers.sh`: it must print PASS. An include that
reaches a higher stratum is a real architectural gap — move the shared
declaration down, or invert the dependency; never silence the checker.

## 6. `modules/empty-module` — a module directory defining nothing

**What it is.** A directory under `kernel/src` with no function definition.

**Fill it.** Implement the module the name promises (with its own test),
or remove the directory and its stratum line. An empty module is a claim
with nothing behind it.

## The quality bar for every fill

1. **Complete code only** — no `TODO`, no stub, no placeholder branch.
   If it cannot be finished, it is not committed (the repository's rule).
2. **Prove the test can fail** — break the code the test guards, watch the
   test fail, restore. A test that never failed proves nothing.
3. **The acceptance test is the node's own verdict** — a manifest is done
   when `loaded, N checks passed`; a module is done when its check passes
   in `make -C kernel verify-all`.
4. **Exact arithmetic only** — no float or double for money, tokens,
   settlement, ledgers or exact measurements; integers with a fixed scale
   or `kernel/src/rational`.
5. **Four-valued truth** — truth entries use `true`, `false`, `both`,
   `neither`, never 0/1, and the kernel's `lpres_conjoin`/`lpres_disjoin`
   (chain-ordered, not Belnap's lattice).
6. **Layered and bounded** — every module has a stratum, no include
   reaches higher, real-time paths use pre-allocated bounded rings with an
   explicit full check.

## The loop

```
while gaps remain:
    python3 tools/agent_loop/gap_audit.py --json /tmp/gaps.json
    for each gap: fill it by its recipe
    re-run the detector: the gap count must strictly fall
    run the acceptance tests (the node's verdict, verify-all, verify_layers)
    commit only when the checks pass
```

The goal is a single number: **zero gaps**. Not "few", not "known" —
zero, with every acceptance test green, so the vertical chain holds from
the bootloader to the browser to the 407 apps.
