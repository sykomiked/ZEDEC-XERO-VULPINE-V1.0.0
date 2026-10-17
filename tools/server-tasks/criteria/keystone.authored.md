## The authored entry table (the complete brief — the node will check every row)

| entry | kind | args | language | what it computes | checks (inputs → answers) |
|---|---|---|---|---|---|
| `setting_limit_ok` | truth | `value,min,max` | eve | true when `min ≤ value ≤ max` (every setting's one law: brightness, volume, text size) | `value=50,min=0,max=100` → `true`; `value=101,min=0,max=100` → `false` |
| `budget_share_x100` | num | `app,total` | kron | the app's share of the budget in hundredths of a percent, floor | `app=25,total=100` → `2500`; `app=1,total=3` → `3333` |
| `budget_sum_ok` | truth | `a,b,c,d,total` | trebago | true when the four app budgets sum to at most `total` | `a=25,b=25,c=25,d=25,total=100` → `true`; `a=25,b=25,c=25,d=26,total=100` → `false` |

Inline shape:

```
provides setting_limit_ok truth value,min,max
logic    eve setting_limit_ok <<
in_range(value, min, max) = (value >= min) && (value <= max)
.
check setting_limit_ok value=50,101 min=0,0 max=100,100 = true,false
```
