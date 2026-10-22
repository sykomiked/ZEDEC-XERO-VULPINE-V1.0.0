# Wave 5 task: Gazette (slot 36), News and feeds
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 36, build position 60 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Caravan (layer 90, wave 1, built before this one: `use caravan 1`); Seedbank (layer 33, wave 1, built before this one: `use seedbank 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: read what nodes and people post (`social.feed`, `chat.feed`, `web4.feed`, `pspace.events`, `irc.record`); follow sources (`chat.follow`). Fetching outside syndication feeds has no method: use only existing methods; list what's missing in your report.
Logic (critical): "show this item" (source followed, not muted) in eve + kron witnessed; unread counts per source in kron + trebago; read-state index in myra.
Keep the daemon's ranking; never rank by engagement.
needs: net, peers, notify.
Planned (no runtime yet): Go.
