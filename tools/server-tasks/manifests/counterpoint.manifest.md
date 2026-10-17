# counterpoint — the complete manifest (the parameter document)

**Purpose.** Shares money, plans and chores as a couple, with joint goals and each person's view kept: joint accounts (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.hold`), plans in the Hoard (`files.read`, `files.write`), the shared clock (`sys.info`).

**Matrix layer 378 · icon group L3 · wave spec `wave31/378-counterpoint.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id counterpoint
matrix 378
name Counterpoint
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs sys.read
needs peers
needs notify

provides limit_excess num value limit
provides exact_total num n a b c d
provides count_ok truth count cap
provides share_x100 num part whole
provides composite_key num high low
provides running_state truth state event
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
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
logic kron composite_key <<
0|_high*1000+_low
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — above it both approve) in Ada as Eve + COBOL as Sutra witnessed

**The clause it implements.** above it both approve) in Ada as Eve + COBOL as Sutra witnessed

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

### Entry 2 — - `household`: household tree in MUMPS as MYRA

**The clause it implements.** - `household`: household tree in MUMPS as MYRA

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

### Entry 3 — never re-derive the tables

**The clause it implements.** never re-derive the tables

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

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

