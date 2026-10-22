\ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
\ SPDX-License-Identifier: Apache-2.0
\ Safe to start normally, as a number (lifeboat_rules.h LB3): 1 when the
\ journal, the books and the backup each passed (1) and the room above the
\ floor is at least 1 byte, else 0; a flag that is not 0 or 1, or a room
\ past 18 digits either way, is refused. ( journal books room tested -- )
VARIABLE BAD VARIABLE ALL
: FLAG ( f -- ) DUP 0 < OVER 1 > OR BAD @ OR BAD ! ALL @ * ALL ! ;
: ROOM ( r -- ) DUP -999999999999999999 < OVER 999999999999999999 > OR BAD @ OR BAD ! 1 < IF 0 ALL ! THEN ;
: START ( j b r t -- ) 0 BAD ! 1 ALL ! FLAG ROOM FLAG FLAG BAD @ IF ." REFUSED" ELSE ALL @ . THEN ;
START CR
