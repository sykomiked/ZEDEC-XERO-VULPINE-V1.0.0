# Wave 10 task: Pawprint (slot 122), Debugger
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 122, build position 118 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: step, break and inspect a Playground program (`play.run` verdicts with line and column) and read zxvd's events (`log.tail`, `journal.query`). zxvd has no attach, breakpoint or step method: use only existing methods; list what's missing in your report.
Logic (critical): "may attach" = own program AND owner approved AND not a system process, in eve + kron witnessed; breakpoint hit (line in the sorted breakpoint vector) in kron + trebago witnessed; steps and instructions counted exact in trebago; limits (at most 256 breakpoints, stack view at most 64 frames) in eve; breakpoint record (file, line, condition, hits) in myra.
COBOL owns no decision here; it is left out.
needs: sys.read, journal.
Planned (no runtime yet): OCaml, JCL.
