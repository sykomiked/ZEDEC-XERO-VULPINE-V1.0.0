# quay — the complete manifest (the parameter document)

**Purpose.** runs a port or marina: berths, arrivals, cargo handling, customs papers and fees (`place.list`, `files.read`, `files.write`, `ledger.entries`, `ledger.post`, `print.submit`).

**Matrix layer 316 · icon group L3 · wave spec `wave26/316-quay.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id quay
matrix 316
name Quay
needs location
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs print
needs notify

provides count_ok truth count cap
provides ratio_x1000 num num den
logic eve count_ok <<
eve {app}
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check count_ok count=2 cap=100 = true
check count_ok count=99 cap=100 = true
check count_ok count=101 cap=100 = false
check count_ok count=100 cap=100 = true
check count_ok count=0 cap=100 = false
check count_ok count=1 cap=100 = true
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `berth_schedule` (arrivals to berths with no overlap, earliest first) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** `berth_schedule` (arrivals to berths with no overlap, earliest first) in FORTRAN as Trebago + APL as KRON witnessed

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

**Rules that the node enforces for this runtime:** the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

### Entry 2 — `berths` grid in APL as KRON

**The clause it implements.** `berths` grid in APL as KRON

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

**A real block from the built apps in this runtime — copy
this spelling exactly (`used.sutra`):**

```
record used-share
  total: 9(18)
  left: 9(18)
  used: 9(5)
procedure
  compute used = (total - left) * 10000 / total
  answer used
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

**Rules that the node enforces for this runtime:** the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

### Entry 3 — `vessels_cargo` tree in MUMPS as MYRA

**The clause it implements.** `vessels_cargo` tree in MUMPS as MYRA

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic trebago <entry_name> <<
<the source>
.
```

**A real block from the built apps in this runtime — copy
this spelling exactly (`count.tb`):**

```
trebago kind_count
  in  kind(n), want
  out count
  count = sum(i, max(0, min(1, 1 + kind(i) - want)) * max(0, min(1, 1 + want - kind(i))))
end
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

**Rules that the node enforces for this runtime:** the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

