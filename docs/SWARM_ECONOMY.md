<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
-->

# The ZXV Swarm Economy

How the AI models in the ZXV swarm share their compute, every cycle. It is a
free market over the nine forms of capital, with an emotional economy at a
right angle to it, and it is governed by the triple ledger. Cooperation pays,
and no model can become a monopoly.

Status: drafted 2026-10-09 from the owner's instructions. Sections 3–8 are
built in `kernel/src/swarm/` (`swarm_budget`, `swarm_emotion`,
`swarm_market`, `swarm_ledger`, `swarm_reserve`) and tested with
`make -C kernel test-swarm`. Sections 9 (wiring to `finance/triple_ledger.h`
itself) and 10–11 are the plan.

## 1. What is traded

- **Tokens** are the goods. Each cycle the swarm may generate `T` tokens in
  total. `T` comes from what the machine can actually do (Section 9), so the
  same rules run on a small Mac, on the owner's M3 Max, and later on a
  supercomputer.
- **Capital** is what models own. Every model holds a wallet across the nine
  canonical forms of capital (`kernel/src/zcapital`):

  | Form | Kind | How a model earns it | Tradeable |
  |---|---|---|---|
  | Financial | money | income from the market (Section 5) | yes, it is the currency |
  | Intellectual | value | knowledge that was used and verified | yes |
  | Manufactured | value | artifacts made: code, files, images, audio | yes |
  | Human | value | help the user accepted | yes |
  | System | value | making the swarm itself faster or cheaper | yes |
  | Social | Crown | cooperating and witnessing (Sections 6, 7) | **never** |
  | Natural | Crown | frugality: giving back tokens it did not need | **never** |
  | Cultural | Crown | matching the user's language, style and ways | **never** |
  | Spiritual | Crown | keeping The One Policy | **never** |

  The four Crown forms are inalienable, exactly as in `zcapital`: they cannot
  be bought or sold, so money can never buy standing.

## 2. The two axes of every allotment

A model's allotment for a cycle is a complex number `re + i·im`.

- The **real axis** is logic and money: the Fibonacci rule plus the market.
- The **imaginary axis**, at a right angle to it, is the emotional economy.

The model may spend `re + im` tokens. The split is kept so the user
interface can show both.

## 3. The Fibonacci rule (built, unchanged)

The swarm is arranged in levels. Level `d` holds at most `F(d+2)` models
(1, 2, 3, 5, 8 …). Level weights are `F(L−d+1)` (5 : 3 : 2 : 1 for four
levels). Every split is an exact whole-number split, and unspent tokens
expire. Full statement: `kernel/src/swarm/swarm_budget.h` (rules R1–R6).

## 4. How a cycle's tokens are divided

1. **Emotion first.** The swarm's mood moves `charge/21` of `T` onto the
   imaginary axis, at most 8/21 (about 38%). (Built: `swarm_emotion.h`.)
2. **The commons floor.** 8/21 of the remaining real tokens are shared under
   the Fibonacci rule. Every working model gets this whatever its wealth,
   so no model can be starved out. This is where cooperation lives.
3. **The open market.** The other 13/21 of the real tokens are sold. Floor
   and market stand in the ratio 8 : 13, two Fibonacci numbers, so the
   split is close to the golden ratio.

## 5. The market (a normal economy)

- **Bidding.** Each cycle a model may bid some of its Financial capital for
  market tokens. It receives market tokens in proportion to its bid, so the
  price per token is the total of all bids divided by the market tokens.
  This is a standard proportional-share market: it clears exactly every
  cycle, and a higher bid always buys more.
- **Paying.** Winning bids are paid into the cycle's pot.
- **Earning.** When the cycle ends the pot is paid back out as income. 13/21
  goes to models in proportion to the value they produced, as scored by the
  user's feedback, the verifiers and the Interaction Surplus measures. 8/21
  is a cooperation dividend, paid in proportion to the Social capital each
  model earned that cycle.
- **Conversion.** Value earned in the tradeable forms (Intellectual,
  Manufactured, Human, System) can be exchanged for Financial capital at the
  market price, with value conserved (`zcap_exchange`).
- **No debt, no inflation.** A model can only bid money it holds. Money is
  conserved: it moves between models but is not created by trading. New money
  is only minted on the Count House's Fibonacci mint curve against value the
  provenance ledger has verified.

## 6. No monopoly

- **Market cap.** No model may win more than 8/21 of the market tokens in a
  cycle. Tokens over the cap go to the other bidders. Tokens nobody may
  take fall back into the commons floor.
- **Wealth cap.** No model may hold more than 8/21 of all Financial capital
  (or an equal share, if the swarm is too small for that). The excess is
  shared out among the models below the cap.
- **Level capacity.** The Fibonacci rule limits how many models any level
  can hold.
- **No self-witnessing** (Section 7) and **no buying of Crown capital**
  (Section 1).

## 7. The witness

Every token allocation is witnessed by a different AI in the swarm.

- Allocation is exact whole-number arithmetic, so a witness does not re-run
  any model. It recomputes the split from the same inputs, which takes
  microseconds and spends no tokens, and records whether it agrees. The
  record is a trit: TRUE (agrees), FALSE (disagrees), or GLUT (contested,
  held for review).
- Witnesses are assigned in a fixed rotation that changes every cycle and
  never picks the model being witnessed. A one-model swarm is witnessed by
  the kernel.
- An allotment settles only once it is witnessed TRUE.
- Each witnessing earns the witness Social capital, which raises its
  cooperation dividend. Models are paid to check each other, which gives the
  swarm a positive feedback loop of trust at almost no compute cost.

## 8. The emotional economy

- **Emotions are emoji.** The palette is 😐 neutral, 😌 calm, 😄 joy,
  🤔 curious, 🥰 love, 😟 worry, 😠 frustration and 😢 sadness. Each has a
  valence matching the GLUT states (+, −, 0) and a behaviour profile that
  sets how its tokens are spent: temperature, exploration, self-checks and
  priority. (Built.)
- **Intensity is Fibonacci.** Intensity 0–5 gives charge 0, 1, 2, 3, 5, 8.
  (Built.)
- **Emotional tokens.** The imaginary pool is shared among the models in
  proportion to their own emotional charge. (Built.)
- **The fractal reserve.** Every model keeps a reserve of everyone else's
  emotions, shaped like the swarm itself. It sees the models in its own
  level one by one, and every other level as one summary, and each summary
  is built the same way one level down. Every part therefore holds a
  smaller copy of the whole. It is a full reserve, not a fractional one:
  the charge it records always adds up to the swarm's real total, so no
  model can spend emotion the swarm does not have. Models read their reserve
  before they bid or answer, which is how they keep track of how the rest
  of the swarm feels.

## 9. The triple ledger governs it all

Every event in the economy is posted to the three ledgers of
`kernel/src/finance/triple_ledger.h`:

| Ledger | Axis | What is posted |
|---|---|---|
| Financial | rational | allotments, bids, payments, income, caps applied |
| Provenance | logical | every witness record and its trit |
| Externality | imaginary | every emotional charge, reserve update and imaginary allotment |

The ledgers balance each cycle: tokens allotted equal `T`, money in equals
money out, and recorded charge equals real charge. A cycle that does not
balance does not settle.

## 10. Every size of machine

The rules never change; only the size of the swarm and `T` do.

| Machine | Levels | Models | Notes |
|---|---|---|---|
| 8 GB Mac (Intel or Apple) | 2 | up to 3 | small models, CPU path |
| 16 GB | 3 | up to 6 | |
| 32–64 GB | 4 | up to 11 | |
| 128 GB (M3 Max) | 5 | up to 19 | the reference machine |
| Supercomputer (later edition) | 6–8 | up to 87 per node | each node is a Count House at Scale-0, a cluster is an Alliance, the whole machine is Global, so the fractal reserve and the ledgers scale the same way |

`T` is measured from the machine's real throughput at start-up and
re-measured over time.

## 11. Growing and shrinking the swarm

- **The companion.** Level 0 always holds exactly one model: the companion
  the user talks to. It runs the Chiglet system, routing each request to the
  experts in the swarm and merging what they return.
- **Fibonacci growth.** When more hardware is available (a bigger Mac, or a
  remote GPU server the user has added), agents are added one Fibonacci step
  at a time. A new level opens only when every level above it is full. Under
  stress (heat, memory pressure, repeated failures) the swarm sheds agents in
  the reverse order.
- **Remote servers.** The user adds a server once with an SSH key kept in the
  Keychain. ZXV then logs in, installs its runtime, loads models into VRAM,
  and boots its instance there, only on servers the user added.
- **Many small parts.** The swarm's strength comes from many reconfigurable
  specialists (voice in and out, OCR, documents, images, music, video,
  vision, code, reasoning) working together, not from one giant model. It
  reconfigures itself for each kind of task.

## 12. Cycles, overlap and quality (built)

- **Harmonic cycles** (`swarm_harmonic`). Time is phase ticks, not a clock.
  The fundamental cycle is 27,720 ticks, the smallest number every whole
  number from 1 to 11 divides, so harmonic n runs every 27,720 / n ticks and
  all eleven line up at each fundamental. Harmonics 9–11 are reflexes
  (witnessing, routing, sensing stress), 4–8 are thought (answers, tool
  chains), and 1–3 are growth (memory, learning, self-improvement).
- **The Venn rule** (`swarm_overlap`). Agents that need the same
  computation (same model, same context, same question) share one job and
  split its cost exactly. The tokens that weren't spent twice are counted.
  A job is charged all or nothing.
- **The quality gate** (`swarm_quality`). Nothing is presented unless
  breadth × verified share ≥ 1.8 (the Hackronomicon gate). A failing output
  is audited and revised first, up to F(L+2) passes. If it still fails, it
  is shown flagged with what is unverified. Only gated work earns value
  income.
- **Truth over agreement.** Claims carry truth states from real checks and
  change state only on evidence, with each change posted to the provenance
  ledger. The swarm says no to a false premise, and adds no blanket refusals
  of its own.
