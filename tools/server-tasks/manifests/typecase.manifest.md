# typecase — the complete manifest (the parameter document)

**Purpose.** preview, install and check fonts kept in the Hoard (`files.list`, `files.read`, `files.write`), and which scripts each covers. zxvd has no font-registry method: use only existing methods; list what's missing in your report.

**Matrix layer 113 · icon group L1 · wave spec `wave2/113-typecase.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id typecase
matrix 113
name Typecase
needs fs.read
needs fs.write

provides count_ok truth count cap
provides share_x100 num part whole
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron share_x100 <<
0|_part*10000%_whole
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

