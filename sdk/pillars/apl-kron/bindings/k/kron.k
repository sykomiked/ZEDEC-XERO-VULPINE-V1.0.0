/ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
/ SPDX-License-Identifier: Apache-2.0
/ kron.k - KRON from ngn/k. Set kronlib to the path of libzkron_k.so
/ (a string), then load this file:   kronlib:"/path/libzkron_k.so"; \l kron.k

/   kron "+/1r3 1r6"       "1r2"           the value text (exact, never a float)
/   kronshow "2 3#!6"      rows, one per line
/   kroncanon "x:1;x+1"    the canonical program text
/   krondigest "1+1"       SHA3-256 of the canonical text, 64 hex chars
/   kronnd "2 2#!4"        (2 2;0 1 2 3;1 1 1 1)   shape;numerators;denominators
/   kronint "2 2#!4"       (0 1;2 3)       when every element is whole
/   kronwhy[]              the full diagnostic of the last refusal

/ A refusal is a K error named by its status ('KR_E_DIVZERO); trap it
/ with .[kron;,"1%0";{x}] and read kronwhy[] for line, column and reason.
/ Truth values come back in kronnd with denominator 0 and the libzxv
/ codes as numerators: 0 neither, 1 true, 2 false, 3 both.

kx:(`$kronlib)2:(`zkron;2)
kronwhy:(`$kronlib)2:(`zkronwhy;1)
kron:kx[0]
kronshow:kx[1]
kroncanon:kx[2]
krondigest:kx[3]
kronnd:{.kx[4;x]}
kronint:{r:kronnd x;$[~&/1=r 2;`err"kronint: not whole";0=#r 0;*r 1;1=#r 0;r 1;(r 0)#r 1]}
