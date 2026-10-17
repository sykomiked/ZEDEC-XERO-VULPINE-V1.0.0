# Criteria (instantiated) — hydrophone

matrix 379 · wave spec `wave31/379-hydrophone.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `zone_ok`: (critical) zone speed limits (a vessel's speed  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `zone_ok`: (critical) zone speed limits (a vessel's speed  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `detect`: (critical) acoustic detection (band energy over  | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| the test asserts state 3 and that the protective zone stays  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `spectro`: spectrogram arrays in APL as KRON
- - `rules`: zone activation rules (a confirmed or BOTH sighting keeps the zone active) in Ada as Eve
- - `levy`: shipping levy (minor units, refusing on overflow) in COBOL as Sutra
- - `species`: species tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the acoustic station detects a whale and the visual watch reports none: the sighting is BOTH
- never re-derive the tables
