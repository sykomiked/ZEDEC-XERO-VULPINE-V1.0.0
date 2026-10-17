# tally — the complete manifest (the parameter document)

**Purpose.** integer and exact rational arithmetic (kernel/src/rational: reduced num/den, overflow refused), units, currency in minor units. Currencies the owner enabled come from `ledger.accounts`. zxvd has no exchange-rate method: use only existing methods; list what's missing in your report. Conversions are shown, never posted.

**Matrix layer 109 · icon group L1 · wave spec `wave2/109-tally.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id tally
matrix 109
name Tally
needs ledger.read

provides ratio_x1000 num num den
provides product num a b
provides composite_key num high low
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic kron product <<
0|_a*_b
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

