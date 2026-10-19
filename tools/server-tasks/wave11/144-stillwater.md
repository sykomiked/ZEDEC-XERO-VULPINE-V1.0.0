# Wave 11 task: Stillwater (slot 144), Focus and do not disturb
Read ../APP_WAVE_SPEC.md first.
What it does: quiet hours and focus modes; notices from `notes.list` (`notes.mark_read`), app limits from `screen.status` and `screen.check`. zxvd has no notice-filter method: use only existing methods; list what's missing in your report.
Logic (critical): "in a quiet window now" (minutes of day, a window may cross midnight) in eve + kron witnessed; "let this notice through" (allowed person, or the same caller twice in 3 minutes, or a Watchfire emergency, always) in eve + kron witnessed; schedule record (mode, days, start, end, allowed) in myra.
A Watchfire emergency is never held.
needs: notify, screen.time.
Planned (no runtime yet): Racket, Mojo, Perl.
