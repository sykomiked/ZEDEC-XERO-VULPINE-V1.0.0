# Wave 2 task: Hedgerow (slot 30), Firewall
Read ../APP_WAVE_SPEC.md first.
What it does: who may reach this machine: network mode (`net.command` off/lan/online), answering peers (`peer.serve`), the phone link (`phone.link`, `phone.list`), switches that expose methods (`features.get`), hosted spaces (`pspace.list`) and the IRC bridge (`irc.status`). Packet filtering and per-port rules have no method: use only existing methods; list what's missing in your report.
Logic (critical): "is this path reachable" (mode, switch, private bind) in eve + kron + C++ wasm (three witnesses); address class (loopback, private, link-local, unique-local, from octets) in Rust wasm + kron witnessed; rule record in myra.
Opening anything is asked first; closing is at once.
needs: net, peers, sys.read, notify.
Planned (no runtime yet): Erlang, HyperTalk, Occam.
