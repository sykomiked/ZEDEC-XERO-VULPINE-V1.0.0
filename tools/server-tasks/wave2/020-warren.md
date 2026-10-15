# Wave 2 task: Warren (slot 20), Services
Read ../APP_WAVE_SPEC.md first.
What it does: zxvd's engines as its feature switches (`features.get`, `features.set`; restart = off then on), their energy (`power.top`, `power.status`), the daemon itself (`zxvd.version`, `sys.info`) and each engine's recent events (`log.tail`). Host background processes outside zxvd have no method: use only existing methods; list what's missing in your report.
Logic (critical): "may this switch change now" (regulated needs approval, owner session) in eve + kron witnessed; restart retries with back-off (attempt n to wait seconds, capped) in basic + kron witnessed; energy share per engine in kron + trebago witnessed; service record in myra.
A regulated switch is never turned on without the owner's approval; never call `zxvd.shutdown` in tests.
needs: sys.read, journal, notify.
Planned (no runtime yet): JCL, IPL.
