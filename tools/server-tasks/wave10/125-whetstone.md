# Wave 10 task: Whetstone (slot 125), Build and test runner
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 125, build position 118 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: run builds and tests and show the verdict. Runs Playground templates with `play.run` (four-valued verdict); reads files with `files.read`. zxvd has no build or test-run method: use only existing methods; list what's missing in your report.
Logic (critical): overall verdict over every result with `lpres_conjoin`'s kernel order (BOTH or NEITHER is not TRUE) in eve + kron witnessed; passed, failed, held counts in kron + sutra witnessed (refused on overflow); durations summed exact in trebago; retry limits (at most 3, timeout 1..3600 s) in eve; run record (target, verdict, counts, ms) in myra.
needs: sys.read, fs.read, notify.
Planned (no runtime yet): JCL, Racket, F# (SDK host only).
