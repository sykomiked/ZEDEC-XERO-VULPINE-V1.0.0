# Wave 15 task: Creel (slot 187), Catch log
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark), wave 6 (073 coffer), wave 7 (088 weatherglass, 093 understory).
Manifest: `slot 187`, `layer L2`, `icon icons/l2/187-creel.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: logs catches, quotas and fishing spots (`place.here`, `place.add`, `place.list`) with weather and sales in the books (`ledger.entries`, `ledger.post`, `ledger.hold`).
Uses: `use coffer 1` (balance, "transfer allowed"); `use waymark 1` (`waymark.place_ok`); `use weatherglass 1` (warnings, readings); `use understory 1` (rolling-window totals). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): quota left (quota minus landed, refused below zero) in COBOL as Sutra + APL as KRON witnessed; "over quota" in Ada as Eve + APL as KRON witnessed; catch sales in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; stock trend (landings per season) exact in FORTRAN as Trebago; catch grid in APL as KRON; vessel tree in MUMPS as MYRA.
BOTH test: the boat logs 420 kg and the dock 380 kg: the catch is BOTH, the larger weight counts against the quota until reconciled, and the sale amount is held (`ledger.hold`).
Advisory; official quota returns are filed by a person; holds and postings are the owner's act.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, ledger.read, ledger.post, notify.
Planned (no runtime yet): Icon.
