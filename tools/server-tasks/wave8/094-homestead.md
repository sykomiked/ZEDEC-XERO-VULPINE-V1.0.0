# Wave 8 task: Homestead (slot 94), Smart home devices
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 94, build position 93 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: the owner's home devices and scenes. No home-device method (devmesh is the owner's computers and phones): use only existing methods; list what's missing in your report.
Logic (critical): "command allowed" (setpoint within the device's range, locks, heaters and ovens need the owner) in eve + forth witnessed; energy totals per device exact in trebago; devices and scenes in myra.
Locks, heat and cooking devices are human-approved; AI suggestions are held, never run.
needs: net, peers, notify.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/homestead/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): IEC 61131-3 Structured Text, Zig.
