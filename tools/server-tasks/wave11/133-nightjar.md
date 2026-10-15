# Wave 11 task: Nightjar (slot 133), Screen reader
Read ../APP_WAVE_SPEC.md first.
What it does: speak what is on screen and where focus is; language of text from `xlate.detect`, simple mode from `ui.get`. zxvd has no accessibility-tree or speech method (kernel/src/speech is not exposed): use only existing methods; list what's missing in your report.
Logic (critical): "interrupt now" (alerts and focus changes cut in, page reading does not) in eve + kron witnessed; speech limits (80..450 words a minute, pitch 50..200) in eve; element record (role, name, row, column, state) in myra; reading order (by row, then column) in kron.
needs: sys.read, notify.
Planned (no runtime yet): OCaml, HyperTalk, Python (SDK host only).
