# dyer — the complete manifest (the parameter document)

**Purpose.** pick colours, convert between RGB, HSL and hex, check contrast, keep palettes in the Hoard (`files.read`, `files.write`). Picking from the screen has no zxvd method: use only existing methods; list what's missing in your report.

**Matrix layer 116 · icon group L1 · wave spec `wave10/116-dyer.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id dyer
matrix 116
name Dyer
needs fs.read
needs fs.write

provides ratio_x1000 num num den
logic kron ratio_x1000 <<
0|_num*1000%_den
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
```

## This app is COMPLETE from the library

No app-specific entries remain. Emit the manifest above exactly;
the node will load every entry and pass every check.

