/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hedgerow_rules.h — Hedgerow's rules in C, the reference its Eve, KRON,
 * C++ and Rust (WebAssembly) and MYRA blocks are checked against
 * (blocks/hedgerow). Pure integer functions, no state: the model
 * (hedgerow_model.c) and the language test (tests/hedgerow_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   HR1 THREE LADDERS of how far a path reaches, each 0, 1 or 2:
 *       the network MODE (0 off, 1 lan, 2 online, as net.command takes it),
 *       a listener's BIND (0 this machine only, 1 the private network,
 *       2 every interface) and how FAR the asker is (0 this machine, 1 the
 *       local network, 2 beyond it).
 *   HR2 ROOM is how many steps of the ladder are left once the asker's
 *       distance is taken: moderoom = mode - far, bindroom = bind - far,
 *       each -2..2 for values on the ladders.
 *   HR3 A PATH IS OPEN (reachable) when the switch that exposes it is on
 *       (sw = 1) and both rooms are 0..2: the asker is no farther than the
 *       mode lets anything in and no farther than the listener binds. A
 *       flag that is no flag (sw not 0 or 1) or a room outside -2..2 is
 *       never read as open: the number form refuses it.
 *   HR4 THE SCOPE OF AN ADDRESS from its octets, for IPv4 (fam 4: a.b.c.d)
 *       and IPv6 (fam 6: a and b its first two octets, c and d its last
 *       two): 0 public, beyond the local network; 1 loopback (127/8, ::1);
 *       2 private (10/8, 172.16/12, 192.168/16); 3 link-local (169.254/16,
 *       fe80::/10); 4 unique-local (fc00::/7); 5 no peer address (IPv4
 *       0/8 and 224/3, multicast and reserved; IPv6 ff00::/8 and ::/16
 *       other than ::1). In ::/16 only c = 0, d = 1 reads as ::1, so the
 *       caller gives c = d = 255 when an octet between them is not 0 (the
 *       model does, HM4). Any other family, or an octet outside 0..255, is
 *       no address: refused.
 *   HR5 FAR OF A SCOPE: loopback 0, private, link-local and unique-local 1,
 *       public 2; no peer address is refused.
 *   HR6 A RULE RECORD files one opening or closing: its sequence number
 *       1..999999999, its path (HEDGEROW_P_*, 1..6), open 0 or 1, the port
 *       0..65535 (0 when the path has none) and when, 0..99999999999
 *       seconds since 1970. */
#ifndef HEDGEROW_RULES_H
#define HEDGEROW_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define HEDGEROW_REFUSED (-1)

/* HR4: the scopes */
enum {
    HEDGEROW_SCOPE_PUBLIC = 0,
    HEDGEROW_SCOPE_LOOPBACK = 1,
    HEDGEROW_SCOPE_PRIVATE = 2,
    HEDGEROW_SCOPE_LINK = 3,
    HEDGEROW_SCOPE_UNIQUE = 4,
    HEDGEROW_SCOPE_NONE = 5,
};

/* HR6: the paths a rule record names */
enum {
    HEDGEROW_P_LAN = 1,    /* net.command lan */
    HEDGEROW_P_ONLINE = 2, /* net.command online */
    HEDGEROW_P_SERVE = 3,  /* peer.serve */
    HEDGEROW_P_PHONE = 4,  /* phone.link */
    HEDGEROW_P_SPACES = 5, /* hosted spaces (pspace) */
    HEDGEROW_P_IRC = 6,    /* the IRC gateway */
    HEDGEROW_PATHS = 6,
};

/* HR3 as a truth: false for a flag that is no flag or a room off the ladder */
static inline bool hedgerow_path_open(int64_t sw, int64_t moderoom, int64_t bindroom)
{
    return sw == 1 && moderoom >= 0 && moderoom <= 2 && bindroom >= 0 && bindroom <= 2;
}

/* HR3 as a number to count with: 1 open, 0 not, HEDGEROW_REFUSED for a
 * flag that is no flag or a room outside -2..2 */
static inline int64_t hedgerow_path_open_n(int64_t sw, int64_t moderoom, int64_t bindroom)
{
    if (sw != 0 && sw != 1) return HEDGEROW_REFUSED;
    if (moderoom < -2 || moderoom > 2 || bindroom < -2 || bindroom > 2) return HEDGEROW_REFUSED;
    return hedgerow_path_open(sw, moderoom, bindroom) ? 1 : 0;
}

/* HR1-HR3 from the ladders themselves; false when one is off its ladder */
static inline bool hedgerow_reach(int64_t sw, int64_t mode, int64_t bind, int64_t far)
{
    if (mode < 0 || mode > 2 || bind < 0 || bind > 2 || far < 0 || far > 2) return false;
    return hedgerow_path_open(sw, mode - far, bind - far);
}

static inline bool hedgerow_octet(int64_t x)
{
    return x >= 0 && x <= 255;
}

/* HR4 */
static inline int64_t hedgerow_addr_scope(int64_t fam, int64_t a, int64_t b, int64_t c, int64_t d)
{
    if (!hedgerow_octet(a) || !hedgerow_octet(b) || !hedgerow_octet(c) || !hedgerow_octet(d))
        return HEDGEROW_REFUSED;
    if (fam == 4) {
        if (a == 127) return HEDGEROW_SCOPE_LOOPBACK;
        if (a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168))
            return HEDGEROW_SCOPE_PRIVATE;
        if (a == 169 && b == 254) return HEDGEROW_SCOPE_LINK;
        if (a == 0 || a >= 224) return HEDGEROW_SCOPE_NONE;
        return HEDGEROW_SCOPE_PUBLIC;
    }
    if (fam == 6) {
        if (a == 0 && b == 0)
            return c == 0 && d == 1 ? HEDGEROW_SCOPE_LOOPBACK : HEDGEROW_SCOPE_NONE;
        if (a == 254 && b >= 128 && b <= 191) return HEDGEROW_SCOPE_LINK;
        if (a == 252 || a == 253) return HEDGEROW_SCOPE_UNIQUE;
        if (a == 255) return HEDGEROW_SCOPE_NONE;
        return HEDGEROW_SCOPE_PUBLIC;
    }
    return HEDGEROW_REFUSED;
}

/* HR5 */
static inline int64_t hedgerow_far_of_scope(int64_t scope)
{
    if (scope == HEDGEROW_SCOPE_LOOPBACK) return 0;
    if (scope == HEDGEROW_SCOPE_PRIVATE || scope == HEDGEROW_SCOPE_LINK ||
        scope == HEDGEROW_SCOPE_UNIQUE)
        return 1;
    if (scope == HEDGEROW_SCOPE_PUBLIC) return 2;
    return HEDGEROW_REFUSED;
}

/* HR6 */
static inline bool hedgerow_rule_ok(int64_t seq, int64_t path, int64_t open, int64_t port,
                                    int64_t stamp)
{
    return seq >= 1 && seq <= 999999999 && path >= 1 && path <= HEDGEROW_PATHS &&
           (open == 0 || open == 1) && port >= 0 && port <= 65535 && stamp >= 0 &&
           stamp <= 99999999999LL;
}

#endif
