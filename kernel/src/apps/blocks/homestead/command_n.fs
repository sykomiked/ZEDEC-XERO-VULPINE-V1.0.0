\ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
\ SPDX-License-Identifier: Apache-2.0
\ A home device command as a number to count with (homestead_rules.h HR3),
\ in Forth: 1 when both setpoint margins are at least 0, no approval is
\ owed and no AI suggested it, else 0. A margin past 199998 either way,
\ owed outside -1 .. 1 or ai outside 0 .. 1 is no measurement: refused.
\ The stack holds above below owed ai, ai on top, one command per call.
VARIABLE ABOVE VARIABLE BELOW VARIABLE OWED VARIABLE AI
: FAR ( n -- flag ) DUP -199998 < SWAP 199998 > OR ;
: STRANGE ( -- flag ) ABOVE @ FAR BELOW @ FAR OR OWED @ -1 < OR OWED @ 1 > OR AI @ 0< OR AI @ 1 > OR ;
: ALLOWED ( -- flag ) ABOVE @ 0< 0= BELOW @ 0< 0= AND OWED @ 0> 0= AND AI @ 0= AND ;
: COMMAND ( above below owed ai -- ) AI ! OWED ! BELOW ! ABOVE ! STRANGE IF ." REFUSED" ELSE ALLOWED IF 1 ELSE 0 THEN . THEN ;
COMMAND CR
