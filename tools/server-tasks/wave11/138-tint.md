# Wave 11 task: Tint (slot 138), Colour and contrast filters
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 138, build position 126 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: colour filters for colour blindness, greyscale, invert, more contrast, fewer flashes. zxvd has no display-filter method (`ui.set` changes simple mode only): use only existing methods; list what's missing in your report.
Logic (critical): flash guard (at most 3 flashes a second, large bright changes held) in eve + kron witnessed; intensity limits 0..100 in eve; filter matrix applied (3x3 integer q12, clamped 0..255) in kron; preset record (name, matrix, intensity) in myra.
needs: sys.read.
Planned (no runtime yet): Natural, Icon, Raku.
