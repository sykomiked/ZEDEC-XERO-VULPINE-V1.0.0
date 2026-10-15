# Wave 8 task: Belvedere (slot 87), Your cloud top
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 87, build position 88 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Seedbank (layer 33, wave 1, built before this one: `use seedbank 1`); Meadow (layer 86, wave 2, built before this one: `use meadow 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: the public face of your space (`spaces.create`, `spaces.get`, `spaces.set_cid`, `pspace.create`, `files.cross`, `web4.cid`).
Logic (critical): "may this file go on the cloud top" (a copy into a more open kind needs the owner's confirmation) in eve + myra witnessed; page byte totals exact in trebago; published items and their CIDs in myra (CIDs come from the daemon only).
Nothing is published without the owner's confirmation.
needs: fs.read, fs.write, peers, net.
Planned (no runtime yet): TypeScript, Logo, Java.
