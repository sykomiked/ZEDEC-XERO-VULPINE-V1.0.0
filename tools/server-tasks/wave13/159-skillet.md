# Wave 13 task: Skillet (slot 159), Kitchen planner
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 8 (082 larder, 089 wellspring, 092 hearth), wave 9 (110 sundial).
Manifest: `slot 159`, `layer L2`, `icon icons/l2/159-skillet.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: turns meal plans into pantry checks, shopping lists and cooking timers for a household, kept in the Hoard (`files.read`, `files.write`), with spend from the books (`ledger.entries`) and time from `sys.info`. zxvd has no method for timer and notice post (as Sundial reports): use only existing methods; list what's missing in your report.
Uses: `use larder 1` (spent per category, "over budget"); `use wellspring 1` (log entries for the household's notes); `use hearth 1` ("contains an excluded allergen", scaled quantities, recipes); `use sundial 1` (timer limits, minutes until). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): shopping list (needed minus in the pantry, never below zero, per item) in APL as KRON + FORTRAN as Trebago witnessed; "keep off the plate" (any household member excludes one of the ingredient's allergens, over the household) in Ada as Eve + APL as KRON witnessed; grocery spend per week in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; portions for the household (Hearth's scaling) in FORTRAN as Trebago; pantry grid in APL as KRON; household tree in MUMPS as MYRA.
BOTH test: ingredient 31 is marked safe in the pantry record and holds nuts in the recipe index: it is BOTH and stays off the plate.
Allergen flags are advisory; the app cannot vouch for product labels.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, ledger.read, sys.read, notify.
Planned (no runtime yet): JCL, IPL, Simula 67.
