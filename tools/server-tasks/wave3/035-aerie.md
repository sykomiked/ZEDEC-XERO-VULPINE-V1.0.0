# Wave 3 task: Aerie (slot 35), Radio and ham links
Read ../APP_WAVE_SPEC.md first.
What it does: messages over radio bands (`ehop.bands`, `ehop.seal`, `ehop.open`), line frames (`harmonic.encode`, `harmonic.decode`), long messages as freight (`freight.pack`, `freight.unpack`). Radio hardware has no method: use only existing methods; list what's missing in your report.
Logic (critical): frame tag split (tag = 100 x sync + 10 x shell + trunk) in forth + kron witnessed; "fits the band" (bytes within the band's largest frame) in eve + kron witnessed; resolver threshold limits in eve; message log in myra.
Messages only, never settlement: no `ledger.*`, no amounts. The owner keeps to the bands their licence allows; Aerie never transmits in tests.
needs: net, notify.
Planned (no runtime yet): Gleam, Perl, LabVIEW.
