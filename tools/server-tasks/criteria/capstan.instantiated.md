# Criteria (instantiated) — capstan

matrix 359 · wave spec `wave30/359-capstan.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `rotor_ok`: (critical) rotor speed limits (rpm at most the | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `rotor_ok`: (critical) rotor speed limits (rpm at most the | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| Paraconsistent (BOTH test, in test_zxapp.c): two vibration s | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| the test asserts state 3 and that an inspection is scheduled | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- a shutdown is the operator's
- - `grid_pay`: (critical) grid payments (kWh x tariff in minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `aero`: blade aerodynamics (power from wind speed by the turbine's integer power curve) in FORTRAN as Trebago
- - `output`: farm output grid (turbines x hours) in APL as KRON
- - `parts`: turbine part tree in MUMPS as MYRA
- never re-derive the tables
