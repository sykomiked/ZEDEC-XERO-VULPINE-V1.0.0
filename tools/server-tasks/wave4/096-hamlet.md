# Wave 4 task: Hamlet (slot 96), Your social circle's feed
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 96, build position 45 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Assay (layer 72, wave 2, an earlier wave: `use assay 1`); Kitling (layer 62, wave 1, an earlier wave: `use kitling 1`); Murmuration (layer 61, built in wave 0: `use murmuration 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: the circle's posts, replies and reactions (`social.feed`, `social.post`, `social.react`, `social.groups`, `social.join`, `social.leave`); gifts to a member through `web4.post` with amount_minor, held until `web4.consent`.
Logic (critical): gift amount (minor units, within the consent ceiling) in sutra + C++ wasm witnessed; "show this post to this seat" (group member, not muted) in eve + myra witnessed; unread counts per group in trebago; read markers in myra. Feed ranking stays the daemon's.
A gift is a financial action: the owner approves it and the receiver consents.
needs: peers, net, notify.
Planned (no runtime yet): Erlang.
