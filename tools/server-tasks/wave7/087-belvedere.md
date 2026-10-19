# Wave 7 task: Belvedere (slot 87), Your cloud top
Read ../APP_WAVE_SPEC.md first.
What it does: the public face of your space (`spaces.create`, `spaces.get`, `spaces.set_cid`, `pspace.create`, `files.cross`, `web4.cid`).
Logic (critical): "may this file go on the cloud top" (a copy into a more open kind needs the owner's confirmation) in eve + myra witnessed; page byte totals exact in trebago; published items and their CIDs in myra (CIDs come from the daemon only).
Nothing is published without the owner's confirmation.
needs: fs.read, fs.write, peers, net.
Planned (no runtime yet): TypeScript, Logo, Java.
