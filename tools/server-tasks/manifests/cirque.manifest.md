# cirque — the complete manifest (the parameter document)

**Purpose.** Draws buildings from plan to 3D, checks measurements and produces drawing sets: projects in the Hoard (`files.read`, `files.write`), sets printed (`print.submit`, `print.status`).

**Matrix layer 227 · icon group L2 · wave spec `wave18/227-cirque.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id cirque
matrix 227
name Cirque
needs fs.read
needs fs.write
needs print

provides exact_total num n a b c d
provides limit_excess num value limit
provides grade_band num value b1 b2 b3 b4
provides count_ok truth count cap
provides composite_key num high low
provides modular_draw num seed members step
provides running_state truth state event
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
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron composite_key <<
0|_high*1000+_low
.
logic kron modular_draw <<
0|(_seed*1103515245+12345+_step*7)%_members
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - structural loads (dead plus live load per span in N/m, integers) in FORTRAN as Trebago, advisory

**The clause it implements.** - structural loads (dead plus live load per span in N/m, integers) in FORTRAN as Trebago, advisory

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

### Entry 2 — never re-derive the tables

**The clause it implements.** never re-derive the tables

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic sutra <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

