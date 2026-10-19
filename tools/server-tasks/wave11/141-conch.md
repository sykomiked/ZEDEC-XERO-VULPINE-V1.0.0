# Wave 11 task: Conch (slot 141), Hearing devices and sound alerts
Read ../APP_WAVE_SPEC.md first.
What it does: pair hearing devices, balance and mono, show sound alerts as a screen flash; alerts from `notes.list`. zxvd has no audio-device method: use only existing methods; list what's missing in your report.
Logic (critical): output level within the cap (per-band gain at most +30 dB in tenths, total at most the owner's cap) in eve + kron witnessed; balance limits (-100..100) in eve; alert mapping (sound kind, flash, vibrate) in myra.
Advisory only: not a clinical fitting; level caps are a safety ceiling the owner sets.
needs: notify, sys.read.
Planned (no runtime yet): Gleam, Raku, LabVIEW.
