\ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
\ SPDX-License-Identifier: Apache-2.0
\ A harmonic line frame's tag from its parts (aerie_rules.h AR1), in
\ Forth: 100 * sync + 10 * shell + trunk; a sync outside 0 .. 1, or a shell
\ or trunk outside 0 .. 9, is refused (tags 200 .. 255 are reserved).
\ The stack holds sync shell trunk, trunk on top, one frame per call.
VARIABLE SYNC VARIABLE SHELL VARIABLE TRUNK
: DIGIT ( n -- flag ) DUP 0< SWAP 9 > OR ;
: STRANGE ( -- flag ) SYNC @ 0< SYNC @ 1 > OR SHELL @ DIGIT OR TRUNK @ DIGIT OR ;
: TAG ( sync shell trunk -- ) TRUNK ! SHELL ! SYNC ! STRANGE IF ." REFUSED" ELSE SYNC @ 100 * SHELL @ 10 * + TRUNK @ + . THEN ;
TAG CR
