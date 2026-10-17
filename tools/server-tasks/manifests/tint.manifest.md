# tint — the complete manifest (the parameter document)

**Purpose.** colour filters for colour blindness, greyscale, invert, more contrast, fewer flashes. zxvd has no display-filter method (`ui.set` changes simple mode only): use only existing methods; list what's missing in your report.

**Matrix layer 138 · icon group L1 · wave spec `wave11/138-tint.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id tint
matrix 138
name Tint
needs sys.read

provides count_ok truth count cap
provides limit_excess num value limit
provides clamp num value lo hi
provides composite_key num high low
logic eve count_ok <<
eve {app}
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check count_ok count=2 cap=100 = true
check count_ok count=99 cap=100 = true
check count_ok count=101 cap=100 = false
check count_ok count=100 cap=100 = true
check count_ok count=0 cap=100 = false
check count_ok count=1 cap=100 = true
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

