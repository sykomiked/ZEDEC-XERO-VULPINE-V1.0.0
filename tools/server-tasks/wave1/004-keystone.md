# Wave 1 task: Keystone (slot 4), Settings
Read ../APP_WAVE_SPEC.md first.
What it does: displays, sound, keyboard, language, accessibility, network, sharing, the Hoard, model choice, budgets (docs/SYSTEM_UTILITIES.md).
Model: read and set settings through zxvd methods that exist (`rpc.discover` lists them); do not invent daemon methods. If a setting has no method, leave it out and list it in your report.
Logic: setting limits (brightness, volume, text size, budgets) in eve + kron witnessed; budget shares across apps in kron + trebago.
needs: sys.read, notify.
