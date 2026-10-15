# Wave 7 task: Hamlet (slot 96), Your social circle's feed
Read ../APP_WAVE_SPEC.md first.
What it does: the circle's posts, replies and reactions (`social.feed`, `social.post`, `social.react`, `social.groups`, `social.join`, `social.leave`); gifts to a member through `web4.post` with amount_minor, held until `web4.consent`.
Logic (critical): gift amount (minor units, within the consent ceiling) in sutra + C++ wasm witnessed; "show this post to this seat" (group member, not muted) in eve + myra witnessed; unread counts per group in trebago; read markers in myra. Feed ranking stays the daemon's.
A gift is a financial action: the owner approves it and the receiver consents.
needs: peers, net, notify.
Planned (no runtime yet): Erlang.
