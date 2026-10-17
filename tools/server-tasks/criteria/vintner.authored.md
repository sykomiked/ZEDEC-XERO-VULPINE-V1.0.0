## The authored entry table (the complete brief — the node will check every row)

| entry | kind | args | language | what it computes | checks (inputs → answers) |
|---|---|---|---|---|---|
| `sale_value` | num | `bottles,price` | sutra | the sale's value in minor units: `bottles × price` exactly | `bottles=1,price=1250` → `1250`; `bottles=12,price=999` → `11988` |
| `volume_conserved` | truth | `harvest,must,barrel,bottle,losses` | trebago | true when `must + barrel + bottle + losses = harvest` exactly (nothing created or destroyed through the stages) | `harvest=100,must=60,barrel=30,bottle=9,losses=1` → `true`; `harvest=100,must=60,barrel=30,bottle=9,losses=2` → `false` |
| `cellar_tenths_ok` | truth | `raw` | forth | true when the raw probe count maps to a tenths-of-a-degree reading within range 0..5000 (0.0..500.0 °C in tenths) | `raw=0` → `true`; `raw=5000` → `true`; `raw=5001` → `false` |
| `lot_label` | num | `vintage,lot` | myra | the lot's key: `vintage × 1000 + lot` (the cellar's index into the lots table) | `vintage=2024,lot=7` → `2024007` |

Inline shape:

```
provides sale_value num bottles,price
logic    sutra sale_value <<
value(bottles, price) = bottles * price
.
check sale_value bottles=1,12 price=1250,999 = 1250,11988
```

Note: two input names, each with its values; the answers pair row by row
(row 1 = the first value of each input).
