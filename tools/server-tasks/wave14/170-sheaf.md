# Wave 14 task: Sheaf (slot 170), Farm ledger
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 4 (073 coffer), wave 7 (040 furrow), wave 8 (088 weatherglass, 093 understory).
Manifest: `slot 170`, `layer L2`, `icon icons/l2/170-sheaf.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: keeps the farm ledger: fields, harvests, livestock, sales and weather in one place, in the books (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.hold`) and the Hoard (`files.read`, `files.write`).
Uses: `use furrow 1` (sums, money cells); `use coffer 1` (balance per account, "transfer allowed"); `use weatherglass 1` (readings); `use understory 1` ("over budget", water and land entries). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): farm accounts per field and enterprise in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; harvest per hectare per field (kilograms) exact in FORTRAN as Trebago; "application allowed" (rate at most the label maximum AND the re-entry interval over) in Ada as Eve + APL as KRON witnessed; scale reading (raw load-cell counts to grams with the calibration pair, range check) in Forth (twin Cairndrake); field grid in APL as KRON; herd tree in MUMPS as MYRA.
BOTH test: the scale reads 1240 kg and the buyer records 1180 kg, tolerance 10: the weight is BOTH and the sale amount is held in escrow (`ledger.hold`) until reconciled; release or refund is the owner's call.
Twin form: the Forth block is committed in its twin form (Cairndrake) and its manifest logic line names `cairndrake`.
Financial: postings and holds are advisory and the owner's act. Chemical limits come from the product label, entered by the farmer.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, ledger.post, fs.read, fs.write, notify.
