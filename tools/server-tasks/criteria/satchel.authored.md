## The authored entry table (the complete brief — the node will check every row)

The limits below are the app's protocol constants (fixed here so the
checks are exact):

| entry | kind | args | language | what it computes | checks (inputs → answers) |
|---|---|---|---|---|---|
| `package_size_limit` | num | `size` | trebago | 0 when `size` ≤ 1048576 (the package ceiling), else the excess `size - 1048576` | `size=0,1048576,1048577,2000000` → `0,0,1,951424` |
| `package_file_count_limit` | num | `count` | eve | 0 when `count` ≤ 4096 (the file ceiling), else the excess | `count=0,4096,4097,10000` → `0,0,1,5904` |
| `package_total_size_exact` | num | `n,s1,s2,s3,s4` | trebago | the exact total of up to 4 file sizes (the accumulator the packer uses; further files extend by the same law) | `n=2,s1=10,s2=20` → `30`; `n=4,s1=1,s2=2,s3=3,s4=4` → `10` |
| `file_share_x100` | num | `size,total` | kron | the file's share of the package in hundredths of a percent, floor | `size=1,total=3` → `3333`; `size=1,total=4` → `2500` |
| `package_record_ok` | truth | `files,size` | myra | true when `files` ≤ 4096 and `size` ≤ 1048576 and `files` ≥ 1 | `files=1,size=0` → `true`; `files=0,size=10` → `false`; `files=4097,size=10` → `false` |

Inline-source shape (the loader requires it — the block does not exist yet):

```
provides package_size_limit num size
logic    trebago package_size_limit <<
# trebago: exact integers, no floating point
size_limit(size) = 0 if size <= 1048576 else size - 1048576
.
check package_size_limit size=0,1048576,1048577,2000000 = 0,0,1,951424
```

Note the check's shape: ONE input name, its values comma-separated, then
`=` and one answer per value. The entry's arg order is the `provides`
line's order.
