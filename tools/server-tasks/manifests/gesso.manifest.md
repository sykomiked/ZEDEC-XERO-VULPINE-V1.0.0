# gesso — the complete manifest (the parameter document)

**Purpose.** images from the Hoard (`files.read`, base64); an edit is saved as a new file (`files.write`), never over the original until the owner says so.

**Matrix layer 53 · icon group L1 · wave spec `wave4/053-gesso.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id gesso
matrix 53
name Gesso
needs fs.read
needs fs.write

provides within_range truth value lo hi
provides ratio_x1000 num num den
provides count_ok truth count cap
provides limit_excess num value limit
provides composite_key num high low
logic eve within_range <<
eve app
values = 0 .. 100

within_range {
  value: values 0 .. 100
}
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron composite_key <<
0|_high*1000+_low
.
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

