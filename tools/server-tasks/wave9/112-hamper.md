# Wave 9 task: Hamper (slot 112), Archives
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 112, build position 108 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: open and make compressed archives in the Hoard (`files.list`, `files.stat`, `files.read`, `files.write`, `files.mkdir`), never over an existing item. zxvd has no archive codec method: use only existing methods; list what's missing in your report.
Logic (critical): every entry in bounds (offset + size within the archive length, over integer vectors) in kron + trebago witnessed; entry path safe (no leading slash, no ".." segment, depth at most 32, as integer counts) in eve + kron witnessed; expansion guard (unpacked total at most 100 x packed and at most free bytes from `sys.disk`) in trebago + kron witnessed; totals (bytes, files) exact in trebago.
An archive that fails any check is refused whole; nothing is written before every check passes.
needs: fs.read, fs.write, sys.read.
Planned (no runtime yet): dBase, Autocode, AWK.
