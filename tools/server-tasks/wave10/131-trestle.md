# Wave 10 task: Trestle (slot 131), Design native screens
Read ../APP_WAVE_SPEC.md first.
What it does: lay out native screens and save them as screen files in the Hoard (`files.read`, `files.write`); simple mode from `ui.get`. zxvd has no toolkit layout method: use only existing methods; list what's missing in your report.
Logic (critical): row and column sizes from minimums and weights (integer shares, remainder to the first) in kron + trebago witnessed; accessibility limits (touch target at least 44 px, text at least 12 px, at most 256 widgets) in eve; widget record (kind, parent, row, column, span) in myra.
COBOL owns no decision here; it is left out.
needs: fs.read, fs.write.
Planned (no runtime yet): Delphi, Racket, Scala.
