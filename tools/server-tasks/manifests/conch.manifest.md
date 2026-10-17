# conch — the complete manifest (the parameter document)

**Purpose.** pair hearing devices, balance and mono, show sound alerts as a screen flash; alerts from `notes.list`. zxvd has no audio-device method: use only existing methods; list what's missing in your report.

**Matrix layer 141 · icon group L1 · wave spec `wave11/141-conch.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id conch
matrix 141
name Conch
needs notify
needs sys.read

provides limit_excess num value limit
provides count_ok truth count cap
provides ratio_x1000 num num den
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

