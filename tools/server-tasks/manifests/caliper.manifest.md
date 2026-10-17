# caliper — the complete manifest (the parameter document)

**Purpose.** measures parts and structures and checks them against design tolerances with exact integer maths, records in the Hoard (`files.read`, `files.write`). zxvd has no method for instrument readings input: use only existing methods; list what's missing in your report.

**Matrix layer 177 · icon group L2 · wave spec `wave14/177-caliper.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id caliper
matrix 177
name Caliper
needs fs.read
needs fs.write

provides count_ok truth count cap
provides ratio_x1000 num num den
provides composite_key num high low
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
logic kron composite_key <<
0|_high*1000+_low
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — measurement arrays in APL as KRON

**The clause it implements.** measurement arrays in APL as KRON

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

### Entry 2 — assembly tree in MUMPS as MYRA

**The clause it implements.** assembly tree in MUMPS as MYRA

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

