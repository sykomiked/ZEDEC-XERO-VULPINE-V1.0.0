<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Propose a pump setpoint, safely

An advisory controller (or an AI assistant) suggests a new setpoint for a
pump: a mode and a flow. Before anyone sees it, the proposal is checked
against hard limits written in Eve, and against two pressure gauges on
the same pipe.

| What happens | Verdict | Result |
|---|---|---|
| everything inside its range, gauges agree | TRUE | a **proposal** for an operator |
| a value outside its range | FALSE | refused, at the line of the limit |
| the two gauges disagree by more than 10.0 kPa | BOTH | **held**: an operator checks the instruments |
| a gauge has not reported | NEITHER | pending: nothing is proposed |

Nothing is clipped to fit: a flow of 300.0 is refused, not quietly turned
into 250.0. Two gauges that disagree are not averaged: one of them may be
broken, and a person decides which to trust.

**This step is advisory. The Playground never switches a pump.** Even a
TRUE result is only a proposal that a person must approve.

## Run it

```sh
zxv new pump-controller
cd pump-controller
zxv run
```

As shipped, the result is TRUE: a proposal ready for an operator.

- To see a **refusal**, set `flow = 300.0` in `inputs.txt`.
- To see a **hold**, set `pressure_b = 470.0`: the gauges now differ by 58.0.
- To see **pending**, delete the `pressure_b` line.

## Files

- `proposal.eve`: the limits, as Eve types (mode, flow, pressure).
- `inputs.txt`: the proposed setpoint and the two gauge readings.
- `template.zxv`: the step, with `sensors=pressure_a,pressure_b tolerance=10.0`.

## Limits

A demonstration of checks, not a control system. It does not talk to a
PLC or SCADA, it does not know your plant, and the ranges here are
examples: use your own, set by your engineers. Its verdicts support an
operator's judgement; they never replace it.
