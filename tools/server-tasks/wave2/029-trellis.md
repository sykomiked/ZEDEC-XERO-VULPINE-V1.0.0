# Wave 2 task: Trellis (slot 29), Peers and mesh links
Read ../APP_WAVE_SPEC.md first.
What it does: pair (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), trust and revoke (`devmesh.devices`, `devmesh.status`, `devmesh.revoke`), phones (`phone.pair`, `phone.list`, `phone.revoke`), peers (`net.peers`, `net.command` off/lan/online).
Logic (critical): "may this device be revoked" (admin session, not this device, not the last admin) in eve + kron witnessed; invitation minutes left in kron; peers per state (active, revoked, waiting) in kron + trebago; peer record in myra.
A revocation can never be undone: the owner confirms it twice. Tests never go online.
needs: peers, net, notify.
Planned (no runtime yet): Go, Elm, Elixir.
