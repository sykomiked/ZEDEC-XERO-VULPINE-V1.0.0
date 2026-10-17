# lamplight — the complete manifest (the parameter document)

**Purpose.** a calm reading view of a document in the Hoard (`files.read`), translated on request (`xlate.translate`, `xlate.detect`), read aloud. zxvd has no text-to-speech method: use only existing methods; list what's missing in your report.

**Matrix layer 139 · icon group L1 · wave spec `wave4/139-lamplight.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id lamplight
matrix 139
name Lamplight
needs fs.read
needs ai.ask

provides limit_excess num value limit
provides ratio_x1000 num num den
provides composite_key num high low
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic kron composite_key <<
0|_high*1000+_low
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

