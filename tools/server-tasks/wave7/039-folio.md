# Wave 7 task: Folio (slot 39), Documents with layout
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 39, build position 73 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: letters, reports and books as files (`files.read`, `files.write`), printed with `print.status`, `print.submit`, `print.job`.
Logic (critical): page fit (lines per page and pages from line count, margins in twips) in kron + C++ wasm witnessed; amount tables in invoices and reports (line totals and the sum, fixed scale, refused on overflow) in sutra + C++ wasm witnessed; page size and margin limits in eve; outline record in myra.
Line breaking stays C (text work); say so in the manifest.
needs: fs.read, fs.write, print.
Planned (no runtime yet): TRAC, TeX.
