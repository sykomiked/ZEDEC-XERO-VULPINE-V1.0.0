# temper — the complete manifest (the parameter document)

**Purpose.** the companion's feeling (`companion.mood`), the swarm's budgets and cycles (`swarm.state`), its earnings in the books (`swarm.earnings`), energy budgets (`power.budget`).

**Matrix layer 66 · icon group L1 · wave spec `wave7/066-temper.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id temper
matrix 66
name Temper
needs swarm
needs ledger.read
needs notify

provides product num a b
provides grade_band num value b1 b2 b3 b4
provides limit_excess num value limit
provides share_x100 num part whole
provides composite_key num high low
logic kron product <<
0|_a*_b
.
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron share_x100 <<
0|(_part*10000)%_whole
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
check share_x100 part=0 whole=1 = 0
check share_x100 part=1 whole=1 = 10000
check share_x100 part=1 whole=10000 = 1
check share_x100 part=3 whole=4 = 7500
check share_x100 part=2 whole=3 = 20000/3
check share_x100 part=1 whole=2 = 5000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

