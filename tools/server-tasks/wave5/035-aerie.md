# Wave 5 task: Aerie (slot 35), Radio and ham links
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 35, build position 59 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Murmuration (layer 61, built in wave 0: `use murmuration 1`); Halloo (layer 28, wave 3, built before this one: `use halloo 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: messages over radio bands (`ehop.bands`, `ehop.seal`, `ehop.open`), line frames (`harmonic.encode`, `harmonic.decode`), long messages as freight (`freight.pack`, `freight.unpack`). Radio hardware has no method: use only existing methods; list what's missing in your report.
Logic (critical): frame tag split (tag = 100 x sync + 10 x shell + trunk) in forth + kron witnessed; "fits the band" (bytes within the band's largest frame) in eve + kron witnessed; resolver threshold limits in eve; message log in myra.
Messages only, never settlement: no `ledger.*`, no amounts. The owner keeps to the bands their licence allows; Aerie never transmits in tests.
needs: net, notify.
Planned (no runtime yet): Gleam, Perl, LabVIEW.
