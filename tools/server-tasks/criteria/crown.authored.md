## The authored entry table (the complete brief — the node will check every row)

| entry | kind | args | language | what it computes | checks (inputs → answers) |
|---|---|---|---|---|---|
| `in_good_standing` | truth | `bits` | eve | true when NO unappealed reason bit is set: `bits` = 0 only (bit 0 = an unappealed refusal) | `bits=0` → `true`; `bits=1` → `false`; `bits=2` → `true` |
| `split_by_weight` | num | `pool,w1,w2,w3,w4` | sutra | the first recipient's share of `pool` by weight, largest-remainder law: `pool × w1 / (w1+w2+w3+w4)` floor, then +1 while the largest remainders last | `pool=100,w1=1,w2=1,w3=1,w4=1` → `25`; `pool=10,w1=1,w2=1,w3=1,w4=0` → `4` |
| `draw_step` | num | `seed,members,step` | trebago | the sortition's modular walk: `(seed × 1103515245 + 12345 + step × 7) mod members` — integer only, no hashing in the block | `seed=1,members=10,step=0` → `0`; `seed=1,members=10,step=1` → `7` |
| `epoch_total` | num | `pool,fee` | trebago | the epoch's distributable total: `pool - fee` exactly | `pool=1000,fee=89` → `911` |

Inline shape:

```
provides in_good_standing truth bits
logic    eve in_good_standing <<
standing(bits) = (bits == 0)
.
check in_good_standing bits=0,1,2 = true,false,true
```
