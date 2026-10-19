/ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
/ SPDX-License-Identifier: Apache-2.0
/ matrix.k - a table of exact rationals: a multiplication table plus a
/ reshaped sample, reversed top to bottom, with its column sums below.
/ The same program as matrix.apl (x f\: y is APL's outer product).
a:3 4#1r2 1 2 3 -1 0
t:(!3)*\:!4
s:a+t
(|s),+/s
