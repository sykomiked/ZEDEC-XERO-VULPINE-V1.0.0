# Wave 7 task: Granary (slot 64), Models
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 64, build position 80 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Caravan (layer 90, wave 1, built before this one: `use caravan 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: the model slot (`model.info`), room on disk (`sys.disk`), energy (`power.status`). Downloading a model and reading its licence have no method: use only existing methods; list what's missing in your report.
Logic (critical): licence allows this use (licence id in the allowed list, personal or commercial flag) in eve + kron witnessed; "fits on disk" in sutra + kron witnessed; memory need (parameters x bits / 8) exact in trebago; model record in myra.
A model whose licence does not allow the use is never loaded; the owner decides.
needs: net, sys.read, fs.read, fs.write.
Planned (no runtime yet): Mojo, Occam, Simula I.
