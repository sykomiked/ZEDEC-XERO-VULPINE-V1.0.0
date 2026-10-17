# cabochon — the complete manifest (the parameter document)

**Purpose.** gathers accounts, savings goals, bills and investments into one honest picture of your wealth (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.holds`).

**Matrix layer 278 · icon group L3 · wave spec `wave23/278-cabochon.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id cabochon
matrix 278
name Cabochon
needs ledger.read
needs fs.read
needs fs.write
needs notify

provides difference num a b
provides ratio_x1000 num num den
provides limit_excess num value limit
provides count_ok truth count cap
logic kron difference <<
0|_a-_b
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
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `portfolio` grid in APL as KRON

**The clause it implements.** `portfolio` grid in APL as KRON

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

