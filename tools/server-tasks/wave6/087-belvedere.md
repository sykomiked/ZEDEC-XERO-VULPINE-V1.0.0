# Wave 6 task: Belvedere (slot 87), Your cloud top
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 87, build position 66 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Seedbank (layer 33, wave 3, an earlier wave: `use seedbank 1`); Meadow (layer 86, wave 2, an earlier wave: `use meadow 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: the public face of your space (`spaces.create`, `spaces.get`, `spaces.set_cid`, `pspace.create`, `files.cross`, `web4.cid`).
Logic (critical): "may this file go on the cloud top" (a copy into a more open kind needs the owner's confirmation) in eve + myra witnessed; page byte totals exact in trebago; published items and their CIDs in myra (CIDs come from the daemon only).
Nothing is published without the owner's confirmation.
needs: fs.read, fs.write, peers, net.
Planned (no runtime yet): TypeScript, Logo, Java.
