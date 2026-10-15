# Wave 2 task: Commons (slot 27), Chat
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 27, build position 20 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Kitling (layer 62, wave 1, an earlier wave: `use kitling 1`); the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 4 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: rooms on this node (`chat.post`, `chat.feed`, `chat.thread`, `chat.react`, `chat.follow`, `chat.me`), groups (`social.groups`, `social.group_create`, `social.join`, `social.leave`, `social.post`), rooms across nodes (`pspace.create`, `pspace.join`, `pspace.post`, `pspace.events`, `pspace.members`) and the IRC bridge (`irc.status`, `irc.bridge`, `irc.record`). Direct messages have no method of their own: use only existing methods; list what's missing in your report.
Logic (critical): "may I post here" (member, room joined, switch on) in eve + kron witnessed; text limits (1..1024 bytes, reaction 1..32) in eve; unread counts per room in kron + trebago; room and message index in myra.
Feeds keep the daemon's ranking; nothing is re-ranked by likes or views.
needs: peers, net, notify.
Planned (no runtime yet): Erlang, Icon, Java.
