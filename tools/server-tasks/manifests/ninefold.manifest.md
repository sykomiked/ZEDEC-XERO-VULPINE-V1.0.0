# ninefold — the complete manifest (the parameter document)

**Purpose.** what the owner holds in each of the nine forms (finance/capital_forms.h), from `ledger.accounts` and `ledger.balance`. No method reports holdings by form: use only existing methods; list what's missing in your report.

**Matrix layer 80 · icon group L1 · wave spec `wave1/080-ninefold.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id ninefold
matrix 80
name Ninefold
needs ledger.read
needs swarm

provides share_x100 num part whole
provides limit_excess num value limit
provides product num a b
logic kron share_x100 <<
0|_part*10000%_whole
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron product <<
0|_a*_b
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — holding per form in minor units in sutra + trebago witnessed

**The clause it implements.** holding per form in minor units in sutra + trebago witnessed

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

