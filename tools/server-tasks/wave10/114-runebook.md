# Wave 10 task: Runebook (slot 114), Character viewer
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 114, build position 112 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: every character, emoji and symbol by script and block, with favourites saved in the Hoard (`files.read`, `files.write`); language names from `xlate.languages`. zxvd has no character-data method: use only existing methods; list what's missing in your report.
Logic (critical): valid code point (0..0x10FFFF, not a surrogate, not a noncharacter) in kron + trebago witnessed; UTF-8 length and bytes of a code point in trebago + kron witnessed; block of a code point (search over the block-start vector) and recently used counts in kron.
needs: fs.read, fs.write.
Planned (no runtime yet): Scheme, CPL, Icon.
