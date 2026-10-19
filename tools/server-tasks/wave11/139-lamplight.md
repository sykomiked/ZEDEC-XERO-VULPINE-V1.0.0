# Wave 11 task: Lamplight (slot 139), Reading view and read aloud
Read ../APP_WAVE_SPEC.md first.
What it does: a calm reading view of a document in the Hoard (`files.read`), translated on request (`xlate.translate`, `xlate.detect`), read aloud. zxvd has no text-to-speech method: use only existing methods; list what's missing in your report.
Logic (critical): layout limits (line 45..90 characters, text 12..72 px, read-aloud 80..450 words a minute) in eve + kron witnessed; minutes to read (words / rate, rounded up) in kron; bookmark record (file, offset, time) in myra.
needs: fs.read, ai.ask.
Planned (no runtime yet): OCaml, Python (SDK host only), TRAC.
