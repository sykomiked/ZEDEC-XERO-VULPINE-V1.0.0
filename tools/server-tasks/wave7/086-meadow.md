# Wave 7 task: Meadow (slot 86), The Park
Read ../APP_WAVE_SPEC.md first.
What it does: find, join and use public spaces (`pspace.find`, `pspace.join`, `pspace.leave`, `pspace.list`, `pspace.members`, `pspace.events`, `pspace.post`, `spaces.list` for park rights). Feature `pspace` is off by default.
Logic (critical): "may this seat post here" (the space kind's rights, post 1..1024 bytes) in eve + myra witnessed (myra looks the seat's role up in the rights record); member and event counts per space in trebago; joined spaces and the last event index in myra.
Posts cannot be deleted (`pspace.post`): the app says so before sending.
needs: peers, net, notify.
Planned (no runtime yet): Logo, C#.
