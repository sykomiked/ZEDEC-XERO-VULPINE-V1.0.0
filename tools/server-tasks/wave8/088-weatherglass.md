# Wave 8 task: Weatherglass (slot 88), Weather
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 88, build position 89 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: local conditions and forecast for where the machine is (`place.here`, `place.ask`). There is no weather source: use only existing methods; list what's missing in your report.
Logic (critical): unit conversion (tenths of a degree C to F, mm, km/h) in trebago + kron witnessed; frost, heat and wind warnings (integer thresholds) in eve + trebago witnessed; 7-day min, max, mean floor in trebago; readings in myra.
Warnings are advisory and never replace official warnings.
needs: location, net, notify.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/weatherglass/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Kotlin.
Data (outside data, docs/DATAPACKS.md): the core pack `weatherglass-core.tar` carries MET Norway Locationforecast (CC BY 4.0) and US NWS alerts (public domain) as live endpoints the app reads over TLS only after the owner approves (`net`), with their attribution; show `ATTRIBUTION.txt` in the app and cache the last reading so it works offline. Recipe: `tools/apps/datapacks/SOURCES.tsv`.
