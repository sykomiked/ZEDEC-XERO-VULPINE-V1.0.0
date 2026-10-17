# apothecary — the complete manifest (the parameter document)

**Purpose.** keeps clinical records and prescriptions in the Hoard (`files.read`, `files.write`) with signed approvals and medical ID. A clinician signs an approval with a passkey (the passkeys methods, under sys.read, added for this). Anything else missing: use only existing methods; list it in your report.

**Matrix layer 180 · icon group L2 · wave spec `wave14/180-apothecary.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id apothecary
matrix 180
name Apothecary
needs fs.read
needs fs.write
needs notify
needs sys.read

provides limit_excess num value limit
provides count_ok truth count cap
provides ratio_x1000 num num den
provides exact_total num n a b c d
provides product num a b
provides composite_key num high low
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
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron product <<
0|_a*_b
.
logic kron composite_key <<
0|_high*1000+_low
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — lab result grid in APL as KRON

**The clause it implements.** lab result grid in APL as KRON

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

