# MASTER — the H100's standing orders

Read this first. It names every document, every task, every known gap and
every bottleneck, and it states the one principle that governs all of
them.

## The principle: the error is the map

An error is not a wall. It is the system telling you exactly which edge
of the tautology is missing. When something fails, the failure **is** the
next task, stated in the failure's own words:

| The error | What it means | The next task |
|---|---|---|
| `HTTP 400 maximum context length` | an input exceeded a bounded segment | segment it (never truncate) |
| `JSONDecodeError` | an envelope or a cap cut the payload | stream it; or segment the source |
| `load failed: expected '^' and a global name` | the MYRA global name exceeded 8 chars | shorten it |
| `check refused (every language)` | the body's semantics do not match the check | re-read the operator table |
| `the languages disagree (BOTH)` | two witnesses genuinely differ | that IS the answer: expect `both` |
| `an input's values are whole numbers` | a check line is malformed | fix the line, not the test |
| `dangling-use` | a `use` names nothing | create the app, or correct the line |
| orphan module | an export nothing calls | integrate it, retire it, or teach the scan |
| a failing test | the code under it is wrong | fix the code; then prove the test fails first |

**Never silence an error. Name it, and let it name the next task.** A
suppressed error is a hole in the tautology; a named one is a step toward
closing it.

## The read order

1. **This file** — the map.
2. `ORPHAN_RESOLUTION.md` — how to find and close a break in the wiring.
3. `GAP_FILLING.md` — the six gap kinds and their recipes.
4. `ASSISTANCE.md` — the verified K/pillar operator facts (read before drafting).
5. `LP_EXTENSION.md` — how to add logic values without breaking anything.
6. `VERIFICATION_TOPOLOGY.md` — the four test tiers, negative space, certificates.
7. `tools/server-tasks/exhaustive/<app>.md` — the per-app specification.
8. `web/apps/names.tsv` — the canonical naming.

## The tasks, in dependency order

### A. The apps (the one real structural gap)
- **State**: 30 built + 377 pending of 407. The pipeline passes ~177 on
  the H100; the state file holds the passed.
- **Run**: `python3 tools/agent_loop/pipeline_parallel.py --workers 8 --attempts 3`
  (VLLM_URL, VLLM_MODEL, ZXVD_URL, ZXVD_TOKEN set). The vLLM must run with
  a window large enough for the full prompt: `--max-model-len 32768`.
- **Never truncate**: the prompt carries the full spec, criteria,
  decoding, skeleton and checks. If a prompt exceeds the window, segment
  it (`tools/agent_loop/segment.py`) — split, parse, join with a digest
  check.
- **Acceptance**: the node's own verdict, `loaded, N checks passed`.
- **The known failure class**: the model's output malformed JSON. The
  fix is streaming (already in `vllm()`), or demanding only the sections.

### B. The OS image
- `make -f build_system/Makefile.x86_64 kernel_x86_64.elf` and
  `make -f build_system/Makefile.arm64 all`. The x86_64 ELF builds clean
  with the ai_layer wired.
- **Acceptance**: zero errors; `build_system/verify_layers.sh` passes;
  `make -C kernel verify-all` green.

### C. The four core accounts (done — keep verified)
- The Crown, Ministry, Fountain and African Union addresses are baked in
  `kernel/src/tribute/tribute.c`, public only. **Never** add a secret to
  the tree, the image, the fleet or Filebase. Verify after any change:
  each baked address equals its local `.address` file, and no passphrase
  or secret key appears anywhere in the repo.

### D. The card rails
- The Dragon/Phoenix/Thunderbird rails are protocol-native
  (`M5_PROTO_DRAGON` in `m5route.h`) and bearer-native
  (`tools/bearer/mk_cards.c`, the issuer ranges 420019/530024/601136).
- **The gap**: no kernel enum beside `TRIBUTE_CROWN`. If the rails must
  be kernel-native, add a `card_rail_t` enum with the three rails and
  their BIN prefixes, and a test asserting the prefixes match the bearer
  tool's.

### E. The wiring (the circuit)
- `python3 tools/agent_loop/wire.py --check` — the netlist, validated.
- `python3 tools/agent_loop/orphan_scan.py` — every module vs its callers.
- `python3 tools/agent_loop/gap_audit.py` — the six gap kinds.
- **Known**: 241 dangling `use` edges (the built apps the graph excludes,
  and block-level references). Resolve each per `ORPHAN_RESOLUTION.md`.

### F. The logic extension
- `LP_EXTENSION.md`. The four values are frozen; extensions go above with
  a chain rank. **First build the mechanism**: `LPRES_STATE_COUNT`, a
  rank table, and count-driven tables. No fifth value until then.

### G. The test code (the ratio)
- **Current**: ~22.7:1. **Floor**: 7:1. **Target**: 100:1.
- The tiers: T1 app checks, T2 inter-language (the `both` expectation),
  T3 inter-app (from the use edges), T4 cross-system, T5 meta, plus the
  bespoke-language suite, the truth tables, and the per-app four-value
  sweeps.
- **Every resolution gets a test that fails before the fix and passes
  after.** A test that never failed proves nothing.

### H. The verification
- `VERIFICATION_TOPOLOGY.md`: Tier 1 static + the negative-space audit
  (`negative_space.sh`), Tier 2 dual-plane parity, Tier 3 the four-valued
  tables + an SMT pass, Tier 4 dynamic stress.
- Certificates: `mk_cert.py` writes a signed `.zxv_cert` into every
  artifact; the loader rejects an unsigned module.

## The bottlenecks, and their fixes

| Bottleneck | The fix |
|---|---|
| The vLLM refuses long prompts (HTTP 400) | run `--max-model-len 32768`; segment, never truncate |
| `urllib` corrupts concurrent requests | the per-call `http.client` connection (done) |
| The model's output malformed JSON | streaming SSE (done); or demand only the sections |
| The prompt too long for a small window | segment the prompt and join the parts |
| The apps' checks refuse | the operator table in `ASSISTANCE.md` |
| A module is orphaned | `ORPHAN_RESOLUTION.md`'s three-way classification |
| A capability is unregistered | add it to `CAPS[]` and give it a provider |

## The omni-tautology — the closing criterion

The system is closed when all five hold, at the current logic arity:

1. **Downward** — every app's `needs` is provided (zero
   `unregistered-need`).
2. **Upward** — every module's export is called (zero orphans).
3. **Sideways** — every `use` resolves (zero `dangling-use`).
4. **Paraconsistent** — where witnesses disagree the join is a non-TRUE
   value and a check expects it.
5. **Layered** — `verify_layers.sh` passes.

Plus the three logic guards (frozen-four, never-authorise, total-table)
and the four verification tiers green.

## The loop (run it continuously)

```
while gaps remain:
    run the detectors (wire, orphan_scan, gap_audit, negative_space)
    for each finding:
        the finding's own message names the task (see the table at the top)
        do the task completely (no stub, no TODO)
        write the test that fails before and passes after
        build, verify, re-run the detector
    the gap count must strictly fall
    commit only when every check passes
```

## What "complete" means

Zero structural gaps, zero orphans, zero dangling uses, every layer
passing, every app loading and passing its checks, every artifact
certified, the logic extensible, and the test ratio climbing — with every
error along the way turned into the next task rather than suppressed.
That is the paraconsistent omni-tautology: it holds even where the parts
contradict, because a contradiction is a value, and an error is a map.

## The per-layer test mandate (every layer, every end)

The tests are not only for the kernel, the OS and the apps. **Every
intervening layer and every piece of our own middleware carries its own
tests**, to the same bar:

| Layer | The test that must exist |
|---|---|
| the kernel core (the scheduler, the memory, the syscalls) | a boot test + a fault-injection test per subsystem |
| each of the 87 modules | a unit test + an integration test with its real callers |
| the middleware (the broker, the bridge, the appkit, the RPC) | a contract test per interface, both directions |
| the network (Vinea, the mesh, the doors) | a two-node test + a lossy-link test |
| the storage (ZXVFS, the ledger, the IPFS node) | a crash-consistency test + a round-trip test |
| the apps | the code test **and** the user-interface test (below) |
| the tools and the build system | a self-test per tool (the tool proves itself) |

**No orphaned codebase**: every directory under `kernel/`, `tools/` and
`web/` must be reached by at least one test and one caller. `orphan_scan.py`
must run over `tools/` and `web/` as well as `kernel/`, and a directory
with no test and no caller is a gap of the `modules/empty-module` kind.

## The dual-end test (both ends of every interface)

Every app is verified from **both** ends:

1. **The code end** — the entries load and their checks pass (the node's
   own verdict: `loaded, N checks passed`).
2. **The user end** — the interface works as a human uses it:
   - every button, hotkey and menu item is **clickable** and triggers its
     action (no dead buttons: a rendered control with a null callback is
     a failure);
   - every action produces a visible state change within the latency
     budget (< 16 ms for 60 Hz responsiveness);
   - every screen has a way out (no trap states);
   - malformed input (a rapid keystroke stream, an out-of-bounds click)
     is handled without a panic.
3. **The parity** — the two ends are the same state machine: an action
   through the AI plane must appear on the human plane, and the reverse
   (`VERIFICATION_TOPOLOGY.md`, Tier 2).

The UI tests are injected headless: synthetic keystrokes and clicks
against the framebuffer, the resulting state compared to the expected.
A button that exists in the manifest but is not clickable is a **dead
button**, and the test names it.
