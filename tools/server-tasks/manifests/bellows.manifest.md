# bellows — the complete manifest (the parameter document)

**Purpose.** requests through the companion (`swarm.ask`, `model.info`) under the owner's budgets (`power.budget`, `power.status`), results held until accepted (M3), then saved (`files.write`). Image and voice generation have no method: use only existing methods; list what's missing in your report.

**Matrix layer 59 · icon group L1 · wave spec `wave7/059-bellows.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id bellows
matrix 59
name Bellows
needs ai.ask
needs swarm
needs fs.write
needs notify

provides limit_excess num value limit
provides product num a b
provides running_state truth state event
provides composite_key num high low
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron product <<
0|_a*_b
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
logic kron composite_key <<
0|_high*1000+_low
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

