# Wave 7 task: Caravan (slot 90), Freight and shipments
Read ../APP_WAVE_SPEC.md first.
What it does: shipments, parcels and addresses (`place.list`, records in the Banker via `files.read`, `files.write`). No shipment or carrier method: use only existing methods; list what's missing in your report.
Logic (critical): charged weight = max(mass, ceil(L x W x H / divisor)) in trebago + sutra witnessed; "within carrier limits" (mass, length, girth) in eve + trebago witnessed; route choice from `use pce_strat 1` (C, advisory); shipment records in myra.
needs: fs.read, fs.write, location, notify.
Planned (no runtime yet): Logo, Objective-C.
