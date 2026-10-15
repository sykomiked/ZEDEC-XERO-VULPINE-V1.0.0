\ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
\ SPDX-License-Identifier: Apache-2.0
\ greatest common divisor by Euclid: ( a b -- ), prints gcd(a, b)
: GCD ( a b -- n ) ABS SWAP ABS SWAP BEGIN DUP WHILE SWAP OVER MOD REPEAT DROP ;
GCD .
