# vintner — the complete manifest (the parameter document)

**Purpose.** vineyard lots, barrels, bottling and sales in the books (`ledger.open_account`, `ledger.post`, `ledger.entries`, `ledger.balance`); barrel lot labels from `grants.status`.

**Matrix layer 91 · icon group L1 · wave spec `wave1/091-vintner.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id vintner
matrix 91
name Vintner
needs ledger.read
needs ledger.post

provides product num a b
provides within_range truth value lo hi
provides conserved_sum truth whole a b c d
provides count_ok truth count cap
logic kron product <<
0|_a*_b
.
logic eve within_range <<
eve app
values = 0 .. 100

within_range {
  value: values 0 .. 100
}
.
logic kron conserved_sum <<
(_a+_b+_c+_d=_whole)
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check conserved_sum whole=100,100,100,101 a=25,0,0,25 b=25,0,0,25 c=25,0,0,25 d=25,100,99,25 = true,true,false,false
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — lots and vintages in myra

**The clause it implements.** lots and vintages in myra

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

