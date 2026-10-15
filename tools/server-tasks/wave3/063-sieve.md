# Wave 3 task: Sieve (slot 63), AI suggestions held until accepted
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 63, build position 36 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 2 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: every held suggestion (M3) in one place: tagged answers (`swarm.ask`), untrusted drafts (`play.draft`), held envelopes (`web4.feed`, `web4.consent`), and what was accepted (`journal.query`).
Logic (critical): "may this be applied" (accepted, not expired, target unchanged since) in eve + kron + C++ wasm (three witnesses); held counts per app in kron + trebago; expiry limits in eve; suggestion record in myra.
Nothing is applied without the owner's accept; an accept is logged.
needs: ai.ask, journal, notify.
Planned (no runtime yet): Python, Occam, Common Lisp.
