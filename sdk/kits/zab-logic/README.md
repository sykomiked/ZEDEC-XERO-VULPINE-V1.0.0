<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZAB-Logic: token budgets and a decision gate for a swarm of agents

ZAB-Logic is a C11 kit for developers who run several AI agents together.
Each cycle it gives every agent a token allotment, and it decides which of
an agent's proposals may run. It is written only on the public libzxv ABI
([`zxv_api.h`](../../libzxv/include/zxv_api.h)), uses no floats and needs
no Python.

- **Budgets.** The swarm economy in libzxv (`zxv_swarm_*`) splits `T`
  tokens per cycle across Fibonacci levels: level `d` holds at most
  F(d+2) agents (1, 2, 3, 5, 8 …), levels weigh F(L−d+1) (3 : 2 : 1 for
  three levels), and the agents in a level share its tokens equally.
  Unspent tokens expire at the end of the cycle.
- **The emotional economy as an input.** The swarm's mood moves
  charge/21 of `T` onto an emotional axis, split by each agent's own
  feeling (an emoji with intensity 0–5, charge 0, 1, 2, 3, 5, 8). Change
  the mood or a feeling and the next cycle's allotments change. ZAB-Logic
  computes none of this: it sets the inputs, reads the library's
  allotments back with `zxv_swarm_allotment`, and reports each agent's
  behaviour profile from `zxv_swarm_emotion_profile`. The rules are in
  [docs/SWARM_ECONOMY.md](../../../docs/SWARM_ECONOMY.md) sections 3, 4 and 8.
- **The decision gate.** Every proposal carries a 64-state hexagram tag
  (ABI 1.3). The gate is the library's action table (`zxv_hex_action`):

  | Tag | Outcome | What happens |
  |---|---|---|
  | polarity `forbidden` (action `rollback`) | `ZAB_QUARANTINED` | refused, and a record (tag, agent, SHA3-256 of the work, reason) is appended to the hash-chained quarantine log (`zxv_hexlog`) |
  | base `both` | `ZAB_HELD` | held for review; `zab_review` gives it a base and polarity and gates it again |
  | base `false` (action `refuse`) | `ZAB_REFUSED` | refused |
  | action `wait`: base `neither`, or `true` with `entropic` evidence | `ZAB_DEFERRED` | not run this time |
  | `true`, action `proceed`, phase not `manifest` | `ZAB_DEFERRED` | not ready |
  | `true`, `proceed`, `manifest`, cost > remaining allotment | `ZAB_OVER_BUDGET` | refused, nothing spent |
  | `true`, `proceed`, `manifest`, cost fits | `ZAB_ADMITTED` | the tokens are spent (`zxv_swarm_consume`) |

  The checks run in that order, so a forbidden `both` tag is quarantined,
  not held.
- **No monopoly.** No agent can spend more than its own allotment, and no
  level can hold more than F(d+2) agents. **Both are enforced by libzxv,
  not by this kit**: `zxv_swarm_consume` never grants past the allotment,
  and `zxv_swarm_register` returns `ZXV_E_CAPACITY` on a full level. The
  kit adds no second cap; its tests prove the library's, on a raw
  `zxv_swarm` and through the kit (a greedy agent is refused, spends
  nothing, and leaves every other agent's share alone).

Kit API version 0.1 (preview), on libzxv ABI 1.3 or later.

## Quickstart

```sh
make -C ../../libzxv all     # once
make test                    # tests, fuzz smoke run, demo, no-float check
make demo                    # three levels of agents over three cycles
```

```c
#include "zab_logic.h"

zxv_ctx *ctx;  zxv_cap *cap;  zab_swarm_t *sw;
zxv_ctx_create(&ctx);
zxv_cap_create(ctx, 1, NULL, ZAB_CAPS_NEEDED, &cap);       /* SWARM | ATTEST */
zab_swarm_create(ctx, cap, 3, 2100, 1024, &sw);            /* 3 levels, T = 2100 */

zab_agent_t conductor = {1, 0, 0x1F60C, 2};                 /* level 0, calm x2 */
zab_agent_t core      = {10, 1, 0x1F914, 3};                /* level 1, curious x3 */
zab_agent_add(sw, &conductor);
zab_agent_add(sw, &core);
zab_set_mood(sw, 0x1F914, 4);                               /* 5/21 of T is emotional */

zab_cycle_begin(sw, NULL);
uint8_t go;  zxv_hex_pack(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_MANIFEST, &go);
zab_proposal_t p = {10, go, 300, (const uint8_t *) "search the docs", 15};
zab_decision_t d;
zab_propose(sw, &p, &d);                     /* d.outcome == ZAB_ADMITTED, d.remaining ... */

zab_cycle_report_t r;
zab_cycle_end(sw, &r);                        /* allotments, use, outcomes, expired, log head */

zab_swarm_destroy(sw);  zxv_cap_destroy(cap);  zxv_ctx_destroy(ctx);
```

`make demo` runs [`examples/three_levels.c`](examples/three_levels.c): a
conductor, two core agents and three specialists, three cycles, every gate
outcome, a held proposal resolved by review, and the quarantine log
listed and verified. Part of its output:

```text
== cycle 2: mood curious x4 (5/21 of T moves to the emotional axis)
cycle 2  T=2100  mood U+1F914 x4
allotted 2100 (emotional 500)  used 570  expired 1530
gate: admitted 3  quarantined 1  held 0  refused 0  deferred 0  over-budget 0
agent       lvl act  allotted  emotional      used  adm qua hld ref def ovr  feeling   temp explore verify prio
1             0 yes       877         77         0    0   0   0   0   0   0  U+1F60C x2    0.500   5.0%      1    1
10            1 yes       382        115       250    1   0   0   0   0   0  U+1F914 x3    1.000  50.0%      1    2
```

## API

All functions return `ZXV_OK` or a negative `ZXV_E_*` code; the detail of
the last failure is in `zab_last_error` (two-call pattern).

| Function | What it does |
|---|---|
| `zab_swarm_create(ctx, cap, levels, T, qcap, &sw)` | a swarm (1–8 levels) and a quarantine log of `qcap` records; `cap` needs `SWARM` and `ATTEST` and must belong to `ctx` (checked) |
| `zab_agent_add(sw, &agent)` | register an agent at a level with a feeling; not while a cycle is open |
| `zab_agent_set_active`, `zab_agent_set_feeling`, `zab_set_mood`, `zab_set_rate` | inputs for the next cycle |
| `zab_cycle_begin(sw, &cycle)` | open a cycle; the library computes every allotment |
| `zab_propose(sw, &proposal, &decision)` | gate one proposal (table above); every outcome is `ZXV_OK` |
| `zab_review(sw, held_id, base, polarity, &decision)` | resolve a held proposal (`zxv_hex_set` keeps its phase) and gate it again |
| `zab_cycle_end(sw, &report)` | close the cycle; the report holds per-agent allotment (real and emotional), use, outcome counts, feeling and behaviour profile, plus the expired tokens and the log head |
| `zab_agent_allotment`, `zab_held_count`, `zab_held_at`, `zab_quarantine_log`, `zab_failed_closed` | read state |
| `zab_report_format`, `zab_outcome_name`, `zab_last_error` | text |

Every call uses the `zxv_cap` given at creation, and libzxv re-checks it
each time: revoke it (or clear its attestation) and the swarm stops with
`ZXV_E_CAP`. Halt the context and every call returns `ZXV_E_POISONED`.

**Fail closed.** If the quarantine log is full, forbidden work cannot be
recorded and must not continue (the rule in `zxv_api.h`): `zab_propose`
returns `ZXV_E_CAPACITY`, the swarm refuses everything after that, and the
kit halts the context with `zxv_ctx_halt`. Size the log for your run.

## Limits

- **It budgets and gates; it does not run agents.** ZAB-Logic decides how
  many tokens an agent may spend and whether a proposal may start. It does
  not run a model, count the tokens a model really generated, or stop an
  agent that ignores a refusal. Your host must spend only what was
  admitted (for a model run by libzxv, bind a budget with
  `zxv_session_bind_budget`).
- **The tag is the agent's claim.** The gate trusts the tag a proposal
  carries. Deciding that work is forbidden, contradictory or ready is the
  job of whatever produces the tag (a verifier, a policy, a contract
  checker such as [z-contract](../z-contract/)), not of this kit.
- **Not a sandbox.** Inside one process a capability is least-privilege
  discipline, not isolation: code in the same address space can make its
  own cap. Use a process boundary ([Tri-Isolate](../tri-isolate/), zxvd)
  where that matters.
- **The economy's limits are the library's.** At most 64 agents and 8
  levels, `T` at most 2^48, per-cycle allotments only (nothing carries
  over). Only the budget and emotion parts of the swarm economy are in the
  ABI; the market, wealth caps, witnesses and the fractal reserve of
  [SWARM_ECONOMY.md](../../../docs/SWARM_ECONOMY.md) are not, so this kit
  does not offer them.
- **The quarantine log is in memory** and hash-chained with SHA-256, as in
  the kernel. Copy it out (`zxv_hexlog_at`) and sign the head yourself if
  it must outlive the process or be shown to someone else.
- **Held proposals** (at most 64) wait across cycles and hold no tokens;
  when reviewed they are charged against the cycle open at that time.
- A `zab_swarm_t` is not thread-safe.

## Tests

| Run | What it covers | Result here |
|---|---|---|
| `make test` (gcc 13, Linux x86-64) | `test/test_zab.c`: 5,177 checks — creation and capability errors, level capacity F(d+2), the library's per-agent cap on a raw `zxv_swarm` and through the kit, all 64 tags at three costs against the documented gate, quarantine records and the hash chain, fail-closed on a full log, held proposals and review, every mood and intensity, 120 random swarm shapes, rate changes, revoked and poisoned contexts, the text report, and determinism across two runs; a 500-swarm fuzz smoke run (81,307 checks); the demo; the no-float scan | 0 failed |
| `make CC=clang test` (clang 18) | the same | 0 failed |
| `make SANITIZE=1 test` (gcc, ASan + UBSan, leak check) and `fuzz_zab 5000` for seeds 1–4 under ASan + UBSan | about 811,000 checks per seed | 0 failed |

The tests compare every allotment with an independent model of the
documented rules ([`test/zab_test.h`](test/zab_test.h)), written from the
rule text rather than the library's code. Changing one rule in that model
(the largest-remainder tie, or the `wait` on entropic evidence) makes both
the tests and the fuzzer fail, so the comparison has teeth.

CI: [`.github/workflows/sdk-agents.yml`](../../../.github/workflows/sdk-agents.yml)
runs `make test`, `make SANITIZE=1 test` and a sanitized fuzz run on
ubuntu-24.04 with gcc, and `make test` on macos-14 with clang. The kit is
compiled with `-mgeneral-regs-only` where the compiler supports it, so a
float in its source would not build.

## Files

| Path | What |
|---|---|
| [`include/zab_logic.h`](include/zab_logic.h) | the API |
| [`src/zab_logic.c`](src/zab_logic.c) | the implementation |
| [`test/test_zab.c`](test/test_zab.c), [`test/fuzz_zab.c`](test/fuzz_zab.c), [`test/zab_test.h`](test/zab_test.h) | tests, fuzzer, check macro and the rules model |
| [`examples/three_levels.c`](examples/three_levels.c) | the demo |

## Licence

Apache-2.0, like the rest of the SDK.
