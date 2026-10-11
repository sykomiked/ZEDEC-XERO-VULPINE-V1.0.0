/ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
/ SPDX-License-Identifier: Apache-2.0
/ stats.k - mean, population variance and range of a sample, exactly.
/ The same program as stats.apl: `zkron same stats.apl stats.k` holds.
x:3 1 4 1 5 9 2 6
n:#x
m:(+/x)%n
v:(+/(x-m)*x-m)%n
m,v,(|/x)-&/x
