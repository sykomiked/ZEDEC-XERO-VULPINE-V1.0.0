# luthier — the complete manifest (the parameter document)

**Purpose.** composes, notates and records music, from instrument loops to finished scores, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for audio capture and playback devices (as Birdsong reports): use only existing methods; list what's missing in your report.

**Matrix layer 157 · icon group L2 · wave spec `wave13/157-luthier.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id luthier
matrix 157
name Luthier
needs fs.read
needs fs.write
needs mic

provides exact_total num n a b c d
provides share_x100 num part whole
provides ratio_x1000 num num den
provides limit_excess num value limit
provides grade_band num value b1 b2 b3 b4
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron share_x100 <<
0|_part*10000%_whole
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — piece hierarchy in MUMPS as MYRA

**The clause it implements.** piece hierarchy in MUMPS as MYRA

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic c <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

