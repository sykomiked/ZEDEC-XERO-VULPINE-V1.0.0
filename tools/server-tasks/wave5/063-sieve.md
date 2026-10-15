# Wave 5 task: Sieve (slot 63), AI suggestions held until accepted
Read ../APP_WAVE_SPEC.md first.
What it does: every held suggestion (M3) in one place: tagged answers (`swarm.ask`), untrusted drafts (`play.draft`), held envelopes (`web4.feed`, `web4.consent`), and what was accepted (`journal.query`).
Logic (critical): "may this be applied" (accepted, not expired, target unchanged since) in eve + kron + C++ wasm (three witnesses); held counts per app in kron + trebago; expiry limits in eve; suggestion record in myra.
Nothing is applied without the owner's accept; an accept is logged.
needs: ai.ask, journal, notify.
Planned (no runtime yet): Python, Occam, Common Lisp.
