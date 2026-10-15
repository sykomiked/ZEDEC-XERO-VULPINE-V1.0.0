# Wave 7 task: Weatherglass (slot 88), Weather
Read ../APP_WAVE_SPEC.md first.
What it does: local conditions and forecast for where the machine is (`place.here`, `place.ask`). There is no weather source: use only existing methods; list what's missing in your report.
Logic (critical): unit conversion (tenths of a degree C to F, mm, km/h) in trebago + kron witnessed; frost, heat and wind warnings (integer thresholds) in eve + trebago witnessed; 7-day min, max, mean floor in trebago; readings in myra.
Warnings are advisory and never replace official warnings.
needs: location, net, notify.
Planned (no runtime yet): Logo, Kotlin.
