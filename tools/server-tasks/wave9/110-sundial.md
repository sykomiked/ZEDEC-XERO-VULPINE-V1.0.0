# Wave 9 task: Sundial (slot 110), Clock
Read ../APP_WAVE_SPEC.md first.
What it does: clock, alarms, timers, world time. Read time from `sys.info`. zxvd has no alarm, timer, time-zone or notice-post method: use only existing methods; list what's missing in your report.
Logic (critical): local time for a zone (UTC minutes + offset, wrapped to 0..1439) in kron + trebago witnessed; minutes until the next alarm (minutes of day, weekday mask) in kron + trebago witnessed; limits (alarms at most 64, snooze 1..60 minutes, timer at most 5999 minutes, a timer never below zero) in Rust wasm + eve witnessed; zone offsets as a vector by zone index in kron.
needs: sys.read, notify.
Planned (no runtime yet): Bash.
