# Criteria (instantiated) — muster

matrix 229 · wave spec `wave19/229-muster.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) power per port (sum of devices' mA within the p | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - (critical) power per port (sum of devices' mA within the p | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - device admission log as fixed records (device, decision, t | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - device admission log as fixed records (device, decision, t | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - device tree (bus, device, driver, state) in MUMPS as MYRA | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| the test asserts state 3 and that the device runs in reduced | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "trust this USB device" = the owner said yes AND its class is allowed AND it is not on the refused list, in Ada as Eve + APL as KRON witnessed
- - bus timing and power figures exact in FORTRAN as Trebago
- - PCI/USB inventory grid (bus x device x function, class, driver) in APL as KRON, with BAR overlap from Mockingbird's block
- Paraconsistent (BOTH test, in test_zxapp.c): a device's firmware descriptor reports class 0x08 (storage) while the ACPI tables describe another device at that address, giving BOTH
- never re-derive the tables
