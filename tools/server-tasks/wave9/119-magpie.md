# Wave 9 task: Magpie (slot 119), Clipboard history
Read ../APP_WAVE_SPEC.md first.
What it does: the last copies, to paste again, pin or clear. Kept in memory only. zxvd has no clipboard method: use only existing methods; list what's missing in your report.
Logic (critical): "keep this copy" = not concealed (password field or marked private) AND within size AND app not excluded, in eve + kron witnessed; history ring (64 slots, head - tail >= capacity drops the oldest unpinned) in kron; bytes held exact, at most 64 x 49152, in trebago.
Never log or store clipboard content in tests; inputs are lengths and flags only.
needs: notify.
Planned (no runtime yet): dBase, TRAC, ALGOL 58.
