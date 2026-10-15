# Wave 9 task: Sundial (slot 110), Clock
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 110, build position 106 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: clock, alarms, timers, world time. Read time from `sys.info`. zxvd has no alarm, timer, time-zone or notice-post method: use only existing methods; list what's missing in your report.
Logic (critical): local time for a zone (UTC minutes + offset, wrapped to 0..1439) in kron + trebago witnessed; minutes until the next alarm (minutes of day, weekday mask) in kron + trebago witnessed; limits (alarms at most 64, snooze 1..60 minutes, timer at most 5999 minutes, a timer never below zero) in Rust wasm + eve witnessed; zone offsets as a vector by zone index in kron.
needs: sys.read, notify.
Planned (no runtime yet): Bash.
