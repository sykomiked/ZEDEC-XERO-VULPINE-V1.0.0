# viaduct — the complete manifest (the parameter document)

**Purpose.** settles payments between currencies, co-ops and networks with fair rates and clear records (`ledger.accounts`, `ledger.post`, `ledger.hold`, `ledger.release`, `netting.open`, `netting.positions`, `netting.close`, `swap.quote`).

**Matrix layer 327 · icon group L3 · wave spec `wave27/327-viaduct.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id viaduct
matrix 327
name Viaduct
needs ledger.read
needs ledger.post
needs peers
needs net
needs notify

provides limit_excess num value limit
provides product num a b
provides ratio_x1000 num num den
provides count_ok truth count cap
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
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `settlement_ledger` (in minor units, refused on overflow) in COBOL as Sutra

**The clause it implements.** `settlement_ledger` (in minor units, refused on overflow) in COBOL as Sutra

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

### Entry 2 — `currencies` grid in APL as KRON

**The clause it implements.** `currencies` grid in APL as KRON

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

