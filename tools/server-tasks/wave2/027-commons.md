# Wave 2 task: Commons (slot 27), Chat
Read ../APP_WAVE_SPEC.md first.
What it does: rooms on this node (`chat.post`, `chat.feed`, `chat.thread`, `chat.react`, `chat.follow`, `chat.me`), groups (`social.groups`, `social.group_create`, `social.join`, `social.leave`, `social.post`), rooms across nodes (`pspace.create`, `pspace.join`, `pspace.post`, `pspace.events`, `pspace.members`) and the IRC bridge (`irc.status`, `irc.bridge`, `irc.record`). Direct messages have no method of their own: use only existing methods; list what's missing in your report.
Logic (critical): "may I post here" (member, room joined, switch on) in eve + kron witnessed; text limits (1..1024 bytes, reaction 1..32) in eve; unread counts per room in kron + trebago; room and message index in myra.
Feeds keep the daemon's ranking; nothing is re-ranked by likes or views.
needs: peers, net, notify.
Planned (no runtime yet): Erlang, Icon, Java.
