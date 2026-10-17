# Criteria (instantiated) — gridmark

matrix 120 · wave spec `wave10/120-gridmark.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| QR version for a length and error level (capacity table) in  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| QR version for a length and error level (capacity table) in  | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| mask penalty per mask (four rules over the module grid) in k | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- EAN-13 check digit in trebago + kron witnessed
- Reed-Solomon error words over GF(256) (log/antilog tables) in kron + trebago witnessed
- A read link is shown, never opened without the person's tap
