# Grid load alerts from two meters

A feeder's load comes from two independent sources: the SCADA system and
the smart-meter total. Each hour, each source says "over the limit" or
"not over". ZXV keeps four answers instead of two:

| Sources say | Verdict | Meaning |
|---|---|---|
| both over | TRUE | an overload alert for the operator |
| both not over | FALSE | no alert |
| one over, one not | BOTH | **held**: a meter fault or a real overload; a person checks |
| no data | NEITHER | pending |

The step is **advisory**. ZXV never trips a breaker or sheds load; it
proposes, and a qualified operator decides.

## Run it

```sh
zxv new grid-load-alerts
cd grid-load-alerts
zxv run
```

As shipped, 19:00 is over the limit on both sources: an alert that needs
an operator's approval. Change the 18:00 smart-meter reading from `495`
to `520` and run again: SCADA says 498 and the meters say 520, so 18:00 is
held as BOTH instead of being guessed either way.

## Draft with the local AI

The Playground tab can ask the local swarm to rewrite `overload.k` (say,
"count a source as over at 95% of the limit"). The draft is checked and
run like anything you type; it never acts by itself.

## Limits

Readings are whole kW here; KRON also takes exact fractions (`1r3`). No
forecast, no network model: this checks readings you give it. It is not
a protection system.
