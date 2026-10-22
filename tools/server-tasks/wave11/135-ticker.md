# Wave 11 task: Ticker (slot 135), Live captions
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 135, build position 123 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: captions for speech in calls, media and the room, translated with `xlate.translate` (language from `xlate.detect`). zxvd has no speech-to-text method: use only existing methods; list what's missing in your report.
Logic (critical): caption stays long enough (at least 1000 ms and 60 ms per character, at most 2 lines of 42) in eve + kron witnessed; caption record (time, speaker, source language, text length) in myra. Line breaking is text and stays C (its best-fit languages have no runtime yet); say so in the manifest.
Captions are not kept unless the person saves them.
needs: mic, ai.ask.
Planned (no runtime yet): Python (SDK host only), SNOBOL, Raku.
