# crown — the complete manifest (the parameter document)

**Purpose.** mutual endowment and distribution by sortition over the grant engine (`grants.status`, `grants.eval`, `grants.epoch`, `grants.pool_credit`, `grants.distribute`, `grants.account`). There is no draw method: use only existing methods; list what's missing in your report (a seeded, verifiable sortition draw).

**Matrix layer 75 · icon group L1 · wave spec `wave3/075-crown.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id crown
matrix 75
name Crown
needs ledger.read
needs ledger.post
needs notify

provides flags_none truth bits forbidden
provides exact_total num n a b c d
provides share_x100 num part whole
provides modular_draw num seed members step
provides count_ok truth count cap
logic kron flags_none <<
(_bits&_forbidden)=0
.
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron share_x100 <<
0|(_part*10000)%_whole
.
logic kron modular_draw <<
0|(((_seed*1103515245)+12345)+(_step*7))|_members
.
logic eve count_ok <<
eve {app}
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
check share_x100 part=0 whole=1 = 0
check share_x100 part=1 whole=1 = 10000
check share_x100 part=1 whole=10000 = 1
check share_x100 part=3 whole=4 = 7500
check share_x100 part=2 whole=3 = 20000/3
check share_x100 part=1 whole=2 = 5000
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check count_ok count=2 cap=100 = true
check count_ok count=99 cap=100 = true
check count_ok count=101 cap=100 = false
check count_ok count=100 cap=100 = true
check count_ok count=0 cap=100 = false
check count_ok count=1 cap=100 = true
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

