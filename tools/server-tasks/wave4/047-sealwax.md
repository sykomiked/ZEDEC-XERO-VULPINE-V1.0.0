# Wave 4 task: Sealwax (slot 47), Fill and sign forms
Read ../APP_WAVE_SPEC.md first.
What it does: forms as files (`files.read`, `files.write`), printed (`print.submit`), a signed copy checked with `web4.verify`. Signing a document with the owner's key has no method: use only existing methods; list what's missing in your report.
Logic (critical): "ready to sign" (every required field filled, each within its bounds) in eve + kron witnessed; amount fields (fixed scale, refused on overflow) in sutra; guided fill order in basic; field record in myra.
Signing is the person's own act: the app never signs or submits by itself; legal and financial forms are advisory.
needs: fs.read, fs.write, print, notify.
Planned (no runtime yet): Icon, PostScript, TeX.
