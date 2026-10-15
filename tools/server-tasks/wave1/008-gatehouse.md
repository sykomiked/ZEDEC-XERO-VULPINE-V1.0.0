# Wave 1 task: Gatehouse (slot 8), Permissions and privacy
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 8, build position 4 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Retort (layer 70, wave 1, an earlier wave: `use retort 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 9 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: which app may use files, camera, microphone, network; which capabilities each holds (`zxv_cap`, `onepolicy`). It reads each manifest's `needs` (zxapp_needs) and shows granted vs asked.
Logic (critical): "is this use allowed" = asked AND granted AND not revoked, in eve + kron + C++ wasm (three witnesses); count of apps per capability in kron + trebago.
needs: sys.read, notify.
