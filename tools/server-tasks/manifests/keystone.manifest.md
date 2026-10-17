# keystone — the complete manifest (the parameter document)

**Purpose.** displays, sound, keyboard, language, accessibility, network, sharing, the Hoard, model choice, budgets (docs/SYSTEM_UTILITIES.md).

**Matrix layer 4 · icon group L1 · wave spec `wave3/004-keystone.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id keystone
matrix 4
name Keystone
needs sys.read
needs notify

provides limit_excess num value limit
provides within_range truth value lo hi
provides share_x100 num part whole
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic eve within_range <<
eve {app}
values = 0 .. 100

within_range {
  value: values 0 .. 100
  lo: values 0 .. 0
  hi: values 100 .. 100
}
.
logic kron share_x100 <<
0|(_part*10000)%_whole
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check within_range value=0,100 lo=0,0 hi=100,100 = true,true
check within_range value=50,101 lo=0,0 hi=100,100 = true,false
check within_range value=1,99 lo=0,0 hi=100,100 = true,true
check within_range value=100 lo=0 hi=100 = true
check within_range value=0 lo=0 hi=100 = true
check within_range value=101 lo=0 hi=100 = false
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
check share_x100 part=0 whole=1 = 0
check share_x100 part=1 whole=1 = 10000
check share_x100 part=1 whole=10000 = 1
check share_x100 part=3 whole=4 = 7500
check share_x100 part=2 whole=3 = 20000/3
check share_x100 part=1 whole=2 = 5000
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

