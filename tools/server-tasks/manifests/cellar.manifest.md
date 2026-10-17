# cellar — the complete manifest (the parameter document)

**Purpose.** volumes, space by folder and space, Trash size, check and repair the Hoard, disc images (`sys.disk`, `files.*`).

**Matrix layer 19 · icon group L1 · wave spec `wave5/019-cellar.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id cellar
matrix 19
name Cellar
needs sys.read
needs fs.read
needs fs.write

provides share_x100 num part whole
provides exact_total num n a b c d
provides limit_excess num value limit
logic kron share_x100 <<
0|(_part*10000)%_whole
.
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
check share_x100 part=0 whole=1 = 0
check share_x100 part=1 whole=1 = 10000
check share_x100 part=1 whole=10000 = 1
check share_x100 part=3 whole=4 = 7500
check share_x100 part=2 whole=3 = 20000/3
check share_x100 part=1 whole=2 = 5000
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

