# Wave 5 task: Brazier (slot 21), Power, battery and heat
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 21, build position 51 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: battery level, charge state, power draw, temperatures. Use only what `sys.info` and other existing methods return; list missing readings in your report.
Logic: battery x100 and minutes left (charge / draw) in sutra + kron witnessed; temperature limits (warn/critical, integer tenths of a degree) in eve + kron witnessed.
needs: sys.read, notify.
