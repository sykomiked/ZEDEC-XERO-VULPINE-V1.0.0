# Wave 5 task: Gazette (slot 36), News and feeds
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 36, build position 58 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Caravan (layer 90, wave 2, an earlier wave: `use caravan 1`); Seedbank (layer 33, wave 3, an earlier wave: `use seedbank 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: read what nodes and people post (`social.feed`, `chat.feed`, `web4.feed`, `pspace.events`, `irc.record`); follow sources (`chat.follow`). Fetching outside syndication feeds has no method: use only existing methods; list what's missing in your report.
Logic (critical): "show this item" (source followed, not muted) in eve + kron witnessed; unread counts per source in kron + trebago; read-state index in myra.
Keep the daemon's ranking; never rank by engagement.
needs: net, peers, notify.
Planned (no runtime yet): Go.
