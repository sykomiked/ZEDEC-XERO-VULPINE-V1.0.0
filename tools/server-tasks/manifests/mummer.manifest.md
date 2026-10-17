# mummer — the complete manifest (the parameter document)

**Purpose.** produces shows: scripts, cues, music and slides in the Hoard (`files.read`, `files.write`) and the audience event as a watch hall (`pspace.create`, `hall.load`, `hall.play`, `hall.pause`, `hall.status`).

**Matrix layer 182 · icon group L2 · wave spec `wave15/182-mummer.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id mummer
matrix 182
name Mummer
needs fs.read
needs fs.write
needs peers
needs net
needs notify

provides exact_total num n a b c d
provides ratio_x1000 num num den
provides difference num a b
provides product num a b
provides limit_excess num value limit
provides count_ok truth count cap
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic kron difference <<
0|_a-_b
.
logic kron product <<
0|_a*_b
.
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
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — cue grid in APL as KRON

**The clause it implements.** cue grid in APL as KRON

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

