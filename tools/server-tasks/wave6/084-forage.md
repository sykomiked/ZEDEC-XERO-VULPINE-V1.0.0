# Wave 6 task: Forage (slot 84), Node bounties
Read ../APP_WAVE_SPEC.md first.
What it does: find and claim work for this node (`net.offers`, `net.command` want/sell, `peer.serve`, `peer.status`, `swarm.state` quest). No bounty board method: use only existing methods; list what's missing in your report.
Logic (critical): bounty value net of the 0.08889% fee in sutra + trebago witnessed; "claim fits" (within the node's swarm and power budget) in eve + sutra witnessed; value per minute ranking exact in trebago; bounty index in myra.
Every claim waits for the owner's approval; `peer.serve` stays off unless the owner turns it on.
needs: peers, net, swarm, notify.
Planned (no runtime yet): Prolog, Eiffel, Solidity.
