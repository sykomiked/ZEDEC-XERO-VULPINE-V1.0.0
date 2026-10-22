\ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
\ SPDX-License-Identifier: Apache-2.0
\ Is the camera ready, as a number to count with (pinhole_rules.h PH2), in
\ Forth: 1 when a sensor is present, the mode is supported and the frame
\ fits the buffer (each 1), else 0; a flag that is not 0 or 1 is refused.
\ The stack holds sensor mode buffer, buffer on top, one camera per call.
VARIABLE BAD VARIABLE ALL
: FLAG ( f -- ) DUP 0 < OVER 1 > OR BAD @ OR BAD ! ALL @ * ALL ! ;
: READY ( s m b -- ) 0 BAD ! 1 ALL ! FLAG FLAG FLAG BAD @ IF ." REFUSED" ELSE ALL @ . THEN ;
READY CR
