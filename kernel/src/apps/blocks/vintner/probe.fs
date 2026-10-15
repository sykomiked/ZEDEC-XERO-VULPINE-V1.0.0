\ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
\ SPDX-License-Identifier: Apache-2.0
\ A cellar probe reading in tenths of a degree (vintner_model.h VN3): the
\ probe counts sixteenths of a degree in -880 .. 2000 (-55 to 125 degrees),
\ tenths = floor(raw * 10 / 16); a count outside that range is refused.
: TENTHS ( raw -- ) DUP -880 < OVER 2000 > OR IF DROP ." REFUSED" ELSE 10 * 16 / . THEN ;
TENTHS CR
