⍝ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
⍝ SPDX-License-Identifier: Apache-2.0
⍝ fold.apl — APL's reduction folds from the right: -/1 2 3 4 is
⍝ 1-(2-(3-4)) = ¯2. fold.k folds from the left and gives ¯8, so the
⍝ two programs have different digests: KRON keeps the meaning, not the
⍝ glyph.
-/1 2 3 4
