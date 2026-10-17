# ferry — the complete manifest (the parameter document)

**Purpose.** move files, spaces, keys (owner approves), settings (`files.cross`, `spaces.*`, Keepsake).

**Matrix layer 12 · icon group L1 · wave spec `wave4/012-ferry.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id ferry
matrix 12
name Ferry
needs fs.read
needs fs.write
needs peers
needs backup

provides exact_total num n a b c d
provides share_x100 num part whole
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
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
check share_x100 part=0 whole=1 = 0
check share_x100 part=1 whole=1 = 10000
check share_x100 part=1 whole=10000 = 1
check share_x100 part=3 whole=4 = 7500
check share_x100 part=2 whole=3 = 20000/3
check share_x100 part=1 whole=2 = 5000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "fits on the target" truth in sutra + kron witnessed

**The clause it implements.** "fits on the target" truth in sutra + kron witnessed

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

