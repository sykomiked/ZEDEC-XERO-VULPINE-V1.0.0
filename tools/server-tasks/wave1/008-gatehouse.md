# Wave 1 task: Gatehouse (slot 8), Permissions and privacy
Read ../APP_WAVE_SPEC.md first.
What it does: which app may use files, camera, microphone, network; which capabilities each holds (`zxv_cap`, `onepolicy`). It reads each manifest's `needs` (zxapp_needs) and shows granted vs asked.
Logic (critical): "is this use allowed" = asked AND granted AND not revoked, in eve + kron + C++ wasm (three witnesses); count of apps per capability in kron + trebago.
needs: sys.read, notify.
