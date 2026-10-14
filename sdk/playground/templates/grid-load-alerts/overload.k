/ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
/ SPDX-License-Identifier: Apache-2.0
/ For each hour, how many sources say "over the limit" and how many say
/ "not over". (sgn d + abs sgn d) % 2 is 1 only when d > 0.
a:((sgn scada-limit)+abs sgn scada-limit)%2
b:((sgn meters-limit)+abs sgn meters-limit)%2
(2,#a)#(a+b),2-a+b
