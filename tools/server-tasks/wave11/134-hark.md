# Wave 11 task: Hark (slot 134), Voice control
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 134, build position 122 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: run commands by voice; free requests go to `swarm.ask` and come back as suggestions held for the person; language from `xlate.detect`. zxvd has no speech-to-text method: use only existing methods; list what's missing in your report.
Logic (critical): "may run by voice" = known command AND confidence at least the threshold AND not a held kind (trash, empty Trash, send, pay, grant, approve) without a spoken confirm, in eve + kron witnessed; limits (threshold 50..100, confirm window 3..30 s) in eve; command phrase table (phrase, command, kind) in myra.
Voice never approves money, grants, deletions or access by itself; those need the person's second confirm.
needs: mic, ai.ask, notify.
Planned (no runtime yet): Mojo, Racket, Perl.
