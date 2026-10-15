# Wave 11 task: Glyphwright (slot 137), Input for every script
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 137, build position 125 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: keyboard layouts and input methods for every script; languages from `xlate.languages`. zxvd has no layout or input-method method: use only existing methods; list what's missing in your report.
Logic (critical): composed character (dead key + base from the table; Hangul L, V, T by 0xAC00 + (L x 21 + V) x 28 + T) in myra + kron witnessed; result is a valid code point (0..0x10FFFF, not a surrogate) in eve; layout record (layout, key, level, code point) in myra.
needs: fs.read.
Planned (no runtime yet): Racket, REXX, TeX.
