# Wave 9 task: Dyer (slot 116), Colour picker and palettes
Read ../APP_WAVE_SPEC.md first.
What it does: pick colours, convert between RGB, HSL and hex, check contrast, keep palettes in the Hoard (`files.read`, `files.write`). Picking from the screen has no zxvd method: use only existing methods; list what's missing in your report.
Logic (critical): RGB to HSL and back in integers (hue 0..359, saturation and lightness 0..1000, channels 0..255 or refused) in kron + trebago witnessed; contrast ratio x100 from relative luminance (integer sRGB table, scale 10^6) with the 450 and 300 pass marks in trebago + kron witnessed.
needs: fs.read, fs.write.
Planned (no runtime yet): JOSS, Perl.
