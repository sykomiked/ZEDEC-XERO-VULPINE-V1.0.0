# Wave 1 task: Brazier (slot 21), Power, battery and heat
Read ../APP_WAVE_SPEC.md first.
What it does: battery level, charge state, power draw, temperatures. Use only what `sys.info` and other existing methods return; list missing readings in your report.
Logic: battery x100 and minutes left (charge / draw) in sutra + kron witnessed; temperature limits (warn/critical, integer tenths of a degree) in eve + kron witnessed.
needs: sys.read, notify.
