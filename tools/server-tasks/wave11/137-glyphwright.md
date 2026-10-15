# Wave 11 task: Glyphwright (slot 137), Input for every script
Read ../APP_WAVE_SPEC.md first.
What it does: keyboard layouts and input methods for every script; languages from `xlate.languages`. zxvd has no layout or input-method method: use only existing methods; list what's missing in your report.
Logic (critical): composed character (dead key + base from the table; Hangul L, V, T by 0xAC00 + (L x 21 + V) x 28 + T) in myra + kron witnessed; result is a valid code point (0..0x10FFFF, not a surrogate) in eve; layout record (layout, key, level, code point) in myra.
needs: fs.read.
Planned (no runtime yet): Racket, REXX, TeX.
