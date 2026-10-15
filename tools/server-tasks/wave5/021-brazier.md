# Wave 5 task: Brazier (slot 21), Power, battery and heat
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 21, build position 52 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: battery level, charge state, power draw, temperatures. Use only what `sys.info` and other existing methods return; list missing readings in your report.
Logic: battery x100 and minutes left (charge / draw) in sutra + kron witnessed; temperature limits (warn/critical, integer tenths of a degree) in eve + kron witnessed.
needs: sys.read, notify.
