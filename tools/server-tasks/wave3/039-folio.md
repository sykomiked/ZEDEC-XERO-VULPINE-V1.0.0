# Wave 3 task: Folio (slot 39), Documents with layout
Read ../APP_WAVE_SPEC.md first.
What it does: letters, reports and books as files (`files.read`, `files.write`), printed with `print.status`, `print.submit`, `print.job`.
Logic (critical): page fit (lines per page and pages from line count, margins in twips) in kron + C++ wasm witnessed; amount tables in invoices and reports (line totals and the sum, fixed scale, refused on overflow) in sutra + C++ wasm witnessed; page size and margin limits in eve; outline record in myra.
Line breaking stays C (text work); say so in the manifest.
needs: fs.read, fs.write, print.
Planned (no runtime yet): TRAC, TeX.
