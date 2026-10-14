/ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
/ SPDX-License-Identifier: Apache-2.0
/ Two sources say where the account closed: the statement's stated
/ closing balance, and its opening balance plus its own lines.
/ Agree: two for, none against (TRUE). Disagree: one for, one against
/ (BOTH, held for a person: we cannot tell which one is wrong).
d:close-open++/lines
a:sgn abs d
(2,1)#(2-a),a
