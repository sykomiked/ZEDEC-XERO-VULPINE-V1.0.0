## The authored entry table (the complete brief — the node will check every row)

| entry | kind | args | language | what it computes | checks (inputs → answers) |
|---|---|---|---|---|---|
| `font_accepted` | truth | `ntables,off,len,size` | kron | true when every table's offset+length stays inside the file, `ntables` ≤ 256 and `size` ≤ 33554432 (32 MiB). Inputs: `ntables`, then the checked table's `off`, `len`, and the file `size` | `ntables=1,off=0,len=16,size=100` → `true`; `ntables=257,off=0,len=16,size=100` → `false`; `ntables=1,off=90,len=16,size=100` → `false` |
| `script_coverage_x100` | num | `covered,total` | trebago | the script's coverage in hundredths of a percent, floor: `covered × 10000 / total` | `covered=50,total=100` → `5000`; `covered=1,total=3` → `3333` |

Inline shape:

```
provides font_accepted truth ntables,off,len,size
logic    kron font_accepted <<
ok(n, off, len, size) = (n <= 256) && (off + len <= size) && (size <= 33554432)
.
check font_accepted ntables=1,257,1 off=0,0,90 len=16,16,16 size=100,100,100 = true,false,false
```
