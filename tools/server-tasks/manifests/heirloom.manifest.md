# heirloom — the complete manifest (the parameter document)

**Purpose.** the inheritance manifest, guardians and recovery (`estate.setup`, `estate.manifest`, `estate.faraid`, `estate.distribute`, `estate.status`, `recovery.setup`, `recovery.status`, `recovery.claim`).

**Matrix layer 81 · icon group L1 · wave spec `wave8/081-heirloom.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id heirloom
matrix 81
name Heirloom
needs ledger.read
needs peers
needs notify

provides exact_total num n a b c d
provides share_x100 num part whole
provides largest_remainder num pool w1 w2 w3 w4
provides composite_key num high low
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron share_x100 <<
0|(_part*10000)%_whole
.
logic kron largest_remainder <<
0|(_pool*_w1)%(_w1+_w2+_w3+_w4)
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
check share_x100 part=0 whole=1 = 0
check share_x100 part=1 whole=1 = 10000
check share_x100 part=1 whole=10000 = 1
check share_x100 part=3 whole=4 = 7500
check share_x100 part=2 whole=3 = 20000/3
check share_x100 part=1 whole=2 = 5000
check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,10/3,7/3,50
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "release due" (k guardian approvals and the waiting period over, no living veto) in eve + trebago witnessed

**The clause it implements.** "release due" (k guardian approvals and the waiting period over, no living veto) in eve + trebago witnessed

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

### Entry 2 — Nothing happens on time or silence alone

**The clause it implements.** Nothing happens on time or silence alone

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

