# Wave 3 task: Gazette (slot 36), News and feeds
Read ../APP_WAVE_SPEC.md first.
What it does: read what nodes and people post (`social.feed`, `chat.feed`, `web4.feed`, `pspace.events`, `irc.record`); follow sources (`chat.follow`). Fetching outside syndication feeds has no method: use only existing methods; list what's missing in your report.
Logic (critical): "show this item" (source followed, not muted) in eve + kron witnessed; unread counts per source in kron + trebago; read-state index in myra.
Keep the daemon's ranking; never rank by engagement.
needs: net, peers, notify.
Planned (no runtime yet): Go.
