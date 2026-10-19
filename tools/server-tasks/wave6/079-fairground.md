# Wave 6 task: Fairground (slot 79), The cooperative market
Read ../APP_WAVE_SPEC.md first.
What it does: what the owner shares, wants and sells, settled sales and fees (`net.offers`, `net.command` share/want/sell, `swarm.state` market, `mesh.books`, `peer.status`). Networking is off by default.
Logic (critical): the 0.08889% assurance fee on a sale in sutra + trebago witnessed (copy blocks/fee_check); "offer allowed" (price and units within the owner's limits) in eve + sutra witnessed; price per unit across offers exact in trebago; offer records as fixed business records in sutra.
Every sell or want waits for the owner's approval.
needs: peers, net, ledger.read, notify.
Planned (no runtime yet): Eiffel, Solidity, SAS.
