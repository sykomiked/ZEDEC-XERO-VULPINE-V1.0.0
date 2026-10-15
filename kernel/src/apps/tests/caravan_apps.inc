/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Caravan's model against zxvd's real dispatcher (LINK): addresses are
 * zxvd's places (place.list), carriers and shipments a file in the Banker
 * read with files.read and written with files.write. */
#include "caravan_model.h"

static caravan_t CV, CV2;

static bool cv_file_is(const char *want)
{
    static uint8_t buf[CARAVAN_TEXT_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(CARAVAN_FILE, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

/* The same shipments, field by field (a struct's padding is no field). */
static bool cv_same(const caravan_t *a, const caravan_t *b)
{
    if (a->nships != b->nships || a->ncarriers != b->ncarriers) return false;
    for (uint32_t i = 0; i < a->ncarriers; i++) {
        const caravan_carrier_t *x = &a->carrier[i], *y = &b->carrier[i];
        if (x->id != y->id || x->mass_max != y->mass_max || x->length_max != y->length_max ||
            x->lg_max != y->lg_max || x->divisor != y->divisor || x->quick != y->quick ||
            x->slow != y->slow)
            return false;
    }
    for (uint32_t i = 0; i < a->nships; i++) {
        const caravan_ship_t *x = &a->ship[i], *y = &b->ship[i];
        if (x->id != y->id || x->carrier != y->carrier || x->status != y->status ||
            x->mass != y->mass || x->stamp != y->stamp || strcmp(x->to, y->to) ||
            memcmp(x->side, y->side, sizeof x->side))
            return false;
    }
    return true;
}

APPS_TEST(caravan)
{
    /* the parcel and the carrier's limits (CR1-CR3) */
    caravan_parcel_t p;
    CHECK(caravan_parcel(500, 100, 300, 200, 5000, &p) == ZXM_OK && p.length == 300 &&
              p.width == 200 && p.height == 100 && p.girth == 600 && p.lg == 900 &&
              p.charged == 1200,
          "parcel: sides sorted, girth 600, charged 1200 g");
    CHECK(caravan_parcel(2000, 300, 200, 100, 5000, &p) == ZXM_OK && p.charged == 2000 &&
              caravan_parcel(0, 301, 201, 101, 5000, &p) == ZXM_OK && p.charged == 1223 &&
              caravan_parcel(-1, 1, 1, 1, 5000, &p) == ZXM_E_ARG &&
              caravan_parcel(1, 100000, 1, 1, 5000, &p) == ZXM_E_ARG &&
              caravan_parcel(1, 1, 1, 1, 0, &p) == ZXM_E_ARG &&
              caravan_parcel(0, 10000, 10000, 1000, 1, &p) == ZXM_E_ARG &&
              caravan_parcel(1, 1, 1, 1, 1, NULL) == ZXM_E_ARG,
          "parcel: the mass wins, rounded up, refusals");
    caravan_carrier_t post = {1, 31500, 1200, 3000, 5000, 24, 72};
    caravan_carrier_t freight = {2, 1000000, 6000, 20000, 6000, 48, 240};
    caravan_carrier_t bad[] = {
        {0, 1, 1, 1, 1, 1, 1},      {1000, 1, 1, 1, 1, 1, 1},      {1, 0, 1, 1, 1, 1, 1},
        {1, 1, 0, 1, 1, 1, 1},      {1, 1, 1, 499996, 1, 1, 1},    {1, 1, 1, 1, 0, 1, 1},
        {1, 1, 1, 1, 100000, 1, 1}, {1, 1, 1, 1, 1, 0, 1},         {1, 1, 1, 1, 1, 5, 4},
        {1, 1, 1, 1, 1, 1, 10000},  {1, 1000000000, 1, 1, 1, 1, 1}};
    CHECK(caravan_carrier_ok(&post) && caravan_carrier_ok(&freight) && !caravan_carrier_ok(NULL),
          "carriers that may be kept");
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(!caravan_carrier_ok(&bad[i]), "a carrier refused: %u", i);
    CHECK(caravan_parcel(31500, 1200, 450, 450, 5000, &p) == ZXM_OK && caravan_within(&post, &p) &&
              caravan_margins(&post, &p).lg == 0,
          "exactly at every limit: inside");
    CHECK(caravan_parcel(31501, 100, 100, 100, 5000, &p) == ZXM_OK && !caravan_within(&post, &p) &&
              caravan_margins(&post, &p).mass == -1 &&
              caravan_parcel(1, 1201, 10, 10, 5000, &p) == ZXM_OK && !caravan_within(&post, &p) &&
              caravan_parcel(1, 1200, 451, 450, 5000, &p) == ZXM_OK && !caravan_within(&post, &p) &&
              caravan_margins(&post, &p).lg == -2,
          "one over each limit: outside");

    /* nothing is written before the file is read */
    CHECK(zxf_use_space(NULL) == 0 && zpl_open(NULL) == 0, "home, places in memory");
    caravan_init(&CV, &LINK);
    int before = sent;
    int64_t id = 0;
    CHECK(caravan_set_carrier(&CV, &post) == ZXM_E_STATE && sent == before && !CV.loaded,
          "not loaded: nothing written");
    CHECK(caravan_set_carrier(&CV, &bad[0]) == ZXM_E_ARG &&
              caravan_add(&CV, 1, 500, 1, 1, 1, "", 1, &id) == ZXM_E_ARG &&
              caravan_add(&CV, 1, 500, 1, 1, 1, "Home", -1, &id) == ZXM_E_ARG &&
              caravan_add(&CV, 1, 500, 1, 1, 1, "Home", 1, NULL) == ZXM_E_ARG &&
              caravan_advance(&CV, 1, 6, 1) == ZXM_E_ARG && caravan_remove(&CV, 1) == ZXM_E_STATE &&
              caravan_choose(&CV, 1, 1, true) == ZXM_E_STATE && sent == before,
          "bad changes are refused before anything is asked");

    /* a file not made yet is empty; the places are zxvd's */
    CHECK(caravan_load(&CV) == ZXM_OK && CV.loaded && !CV.ncarriers && !CV.nships,
          "no file yet: %s", LINK.err);
    CHECK(
        raw_call("place.add", "{\"name\":\"Home\",\"lat\":51507400,\"lon\":-127800}") == ZXM_OK &&
            raw_call("place.add",
                     "{\"name\":\"Aunt Rose\",\"lat\":53480000,\"lon\":-2240000,\"note\":\"x\"}") ==
                ZXM_OK &&
            caravan_places(&CV) == ZXM_OK && CV.nplaces == 2 && CV.place[0].distance_m == -1,
        "places: %s", LINK.err);
    CHECK(caravan_set_carrier(&CV, &freight) == ZXM_OK &&
              caravan_set_carrier(&CV, &post) == ZXM_OK && CV.ncarriers == 2 &&
              CV.carrier[0].id == 1 && CV.carrier[1].id == 2,
          "carriers kept in order: %s", LINK.err);

    /* shipments: only to a kept place, only inside the carrier's limits */
    CHECK(caravan_add(&CV, 1, 500, 300, 200, 100, "Nowhere", 1700000000, &id) == ZXM_E_STATE &&
              caravan_add(&CV, 3, 500, 300, 200, 100, "Home", 1700000000, &id) == ZXM_E_ARG,
          "no such place or carrier");
    before = sent;
    CHECK(caravan_add(&CV, 1, 40000, 1300, 500, 400, "Home", 1700000000, &id) == ZXM_E_ARG &&
              sent == before && CV.nships == 0 &&
              strstr(CV.notice, "Over carrier 1's limits: mass by 8500 g; longest side by 100 "
                                "mm; length and girth by 100 mm; not added."),
          "over the limits: refused, the notice says by how much: %s", CV.notice);
    CHECK(caravan_add(&CV, 1, 500, 100, 300, 200, "aunt rose", 1700000000, &id) == ZXM_OK &&
              id == 1 &&
              caravan_add(&CV, 2, 90000, 2000, 1000, 800, "Home", 1700000100, &id) == ZXM_OK &&
              id == 2 && CV.nships == 2 && !strcmp(CV.ship[0].to, "Aunt Rose"),
          "two shipments: %s", LINK.err);
    CHECK(cv_file_is("caravan 1\ncarrier 1 31500 1200 3000 5000 24 72\n"
                     "carrier 2 1000000 6000 20000 6000 48 240\n"
                     "ship 1 1 500 100 300 200 0 1700000000 Aunt Rose\n"
                     "ship 2 2 90000 2000 1000 800 0 1700000100 Home\n"),
          "the file in the Banker (CM1-CM3)");
    CHECK(caravan_notice(&CV, 1) == ZXM_OK &&
              !strcmp(CV.notice, "Shipment 1 to Aunt Rose: drafted with carrier 1, 1200 g "
                                 "charged (mass 500 g), 24 to 72 hours on the way."),
          "notice: %s", CV.notice);
    CHECK(caravan_notice(&CV, 2) == ZXM_OK && strstr(CV.notice, "266667 g charged (mass 90000 g)"),
          "freight's divisor: %s", CV.notice);

    /* the route question (CM6): every carrier that fits is an option */
    caravan_route_t r;
    CHECK(caravan_route(&CV, 1, &r) == ZXM_OK && r.nopt == 2 && r.carrier[0] == 1 &&
              r.carrier[1] == 2 && r.payoff[0] == -24 && r.payoff[1] == -72 && r.payoff[2] == -48 &&
              r.payoff[3] == -240 && r.lo[0] == 0 && r.hi[1] == 1 && r.den == 1,
          "route: two options, quick and slow");
    CHECK(caravan_route(&CV, 2, &r) == ZXM_OK && r.nopt == 1 && r.carrier[0] == 2 &&
              caravan_route(&CV, 9, &r) == ZXM_E_ARG && caravan_route(&CV, 1, NULL) == ZXM_E_ARG,
          "route: only the freight carrier takes the crate");
    before = sent;
    CHECK(caravan_choose(&CV, 1, 2, false) == ZXM_E_STATE && sent == before &&
              CV.ship[0].carrier == 1,
          "no carrier changes without the owner's word");
    CHECK(caravan_choose(&CV, 2, 1, true) == ZXM_E_ARG && strstr(CV.notice, "Over carrier 1") &&
              caravan_choose(&CV, 1, 2, true) == ZXM_OK && CV.ship[0].carrier == 2,
          "the owner's choice, inside the limits: %s", LINK.err);

    /* moving on (CM7) */
    CHECK(caravan_advance(&CV, 1, CARAVAN_HANDED, 1700000200) == ZXM_E_STATE &&
              caravan_advance(&CV, 1, CARAVAN_LABELLED, 1699999999) == ZXM_E_STATE &&
              caravan_advance(&CV, 1, CARAVAN_LABELLED, 1700000200) == ZXM_OK &&
              caravan_advance(&CV, 1, CARAVAN_RETURNED, 1700000300) == ZXM_E_STATE &&
              caravan_advance(&CV, 1, CARAVAN_HANDED, 1700000300) == ZXM_OK &&
              caravan_choose(&CV, 1, 1, true) == ZXM_E_STATE &&
              caravan_remove(&CV, 1) == ZXM_E_STATE &&
              caravan_advance(&CV, 1, CARAVAN_TRANSIT, 1700000400) == ZXM_OK &&
              caravan_advance(&CV, 1, CARAVAN_DELIVERED, 1700000500) == ZXM_OK &&
              caravan_advance(&CV, 1, CARAVAN_RETURNED, 1700000600) == ZXM_E_STATE &&
              caravan_advance(&CV, 9, CARAVAN_LABELLED, 1) == ZXM_E_ARG &&
              CV.ship[0].status == CARAVAN_DELIVERED,
          "forward only: %s", LINK.err);
    CHECK(caravan_notice(&CV, 1) == ZXM_OK && strstr(CV.notice, ": delivered with carrier 2"),
          "delivered: %s", CV.notice);
    CHECK(caravan_remove(&CV, 9) == ZXM_E_ARG && caravan_remove(&CV, 2) == ZXM_OK &&
              CV.nships == 1 && caravan_notice(&CV, 2) == ZXM_E_ARG,
          "a drafted shipment removed");
    CHECK(caravan_add(&CV, 1, 1, 1, 1, 1, "Home", 1700000700, &id) == ZXM_OK && id == 2,
          "the next id after the last");

    /* a change of limits that would leave a record wrong is refused */
    caravan_carrier_t tiny = {2, 1, 1, 1, 99999, 1, 1};
    CHECK(caravan_set_carrier(&CV, &tiny) == ZXM_OK && CV.carrier[1].mass_max == 1,
          "limits may tighten: records keep their place");
    caravan_init(&CV2, &LINK);
    CHECK(caravan_load(&CV2) == ZXM_OK && CV2.nships == 2 && CV2.ncarriers == 2 &&
              cv_same(&CV2, &CV),
          "read back: %s", LINK.err);

    /* full: more is refused before anything is asked */
    bool ok = true;
    while (ok && CV.nships < CARAVAN_SHIPS)
        ok = caravan_add(&CV, 1, 31500, 1200, 450, 450, "Aunt Rose", CARAVAN_STAMP_MAX, &id) ==
             ZXM_OK;
    CHECK(ok && CV.nships == CARAVAN_SHIPS, "full: %u %s", CV.nships, LINK.err);
    before = sent;
    CHECK(caravan_add(&CV, 1, 1, 1, 1, 1, "Home", 1, &id) == ZXM_E_TOOBIG && sent == before,
          "no room");
    caravan_init(&CV2, &LINK);
    CHECK(caravan_load(&CV2) == ZXM_OK && CV2.nships == CARAVAN_SHIPS && cv_same(&CV2, &CV),
          "a full file read back: %s", LINK.err);
    for (uint32_t i = 0; i < CARAVAN_CARRIERS - 2u; i++) {
        caravan_carrier_t k = {(int32_t) (10 + i), 1, 1, 1, 1, 1, 1};
        CHECK(caravan_set_carrier(&CV, &k) == ZXM_OK, "carrier %u", i);
    }
    caravan_carrier_t more = {999, 1, 1, 1, 1, 1, 1};
    CHECK(caravan_set_carrier(&CV, &more) == ZXM_E_TOOBIG && CV.ncarriers == CARAVAN_CARRIERS,
          "no room for a seventeenth carrier");

    /* a file that is not Caravan's is never overwritten (CM4) */
    uint64_t size;
    static const char zero[] = "caravan 1\ncarrier 01 1 1 1 1 1 1\n";
    CHECK(zxf_write(CARAVAN_FILE, (const uint8_t *) zero, (uint32_t) strlen(zero), ZXF_W_REPLACE,
                    false, &size) == 0,
          "a leading zero");
    CHECK(caravan_load(&CV2) == ZXM_E_PROTOCOL && !CV2.loaded &&
              caravan_set_carrier(&CV2, &post) == ZXM_E_STATE && cv_file_is(zero),
          "not Caravan's: refused, left as it was");
    static const char *const notfile[] = {
        "notes\n",
        "caravan 1\ncarrier 1 1 1 1 1 1 1",
        "caravan 1\ncarrier 2 1 1 1 1 1 1\ncarrier 1 1 1 1 1 1 1\n",
        "caravan 1\ncarrier 1 1 1 1 1 2 1\n",
        "caravan 1\ncarrier 9999999999 1 1 1 1 1 1\n",
        "caravan 1\ncarrier 1 9 9 9 1 1 1\nship 1 2 1 1 1 1 0 1 Home\n",
        "caravan 1\ncarrier 1 9 9 9 1 1 1\nship 1 1 1 1 1 1 6 1 Home\n",
        "caravan 1\ncarrier 1 9 9 9 1 1 1\nship 1 1 1 1 1 1 0 1 \n",
        "caravan 1\ncarrier 1 9 9 9 1 1 1\nship 0 1 1 1 1 1 0 1 Home\n",
        "caravan 1\ncarrier 1 9 9 9 1 1 1\nship 2 1 1 1 1 1 0 1 A\nship 1 1 1 1 1 1 0 1 B\n",
        "caravan 1\ncarrier 1 9 9 9 1 1 1\nship 1 1 1 100000 1 1 0 1 Home\n",
        "caravan 1\ncarrier 1 9 9 9 1 1 1\nship 1 1 1 1 1 1 0 1 Home\ncarrier 2 1 1 1 1 1 1\n",
        "caravan 1\ncarrier 1 9 9 9 1 1 1\nship 1 1 1 1 1 1 0 1 "
        "0123456789012345678901234567890123456789012345678901234567890123\n"};
    for (uint32_t i = 0; i < sizeof notfile / sizeof notfile[0]; i++)
        CHECK(zxf_write(CARAVAN_FILE, (const uint8_t *) notfile[i], (uint32_t) strlen(notfile[i]),
                        ZXF_W_REPLACE, false, &size) == 0 &&
                  caravan_load(&CV2) == ZXM_E_PROTOCOL,
              "not Caravan's: %u", i);
    CHECK(zxf_write(CARAVAN_FILE, (const uint8_t *) "caravan 1\n", 10, ZXF_W_REPLACE, false,
                    &size) == 0 &&
              caravan_load(&CV2) == ZXM_OK && CV2.loaded && !CV2.nships && !CV2.ncarriers,
          "a file with nothing in it");
    zpl_close();

    /* made up replies: a list of places that is not one */
    caravan_init(&CV2, &FAKE);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"places\":[{\"name\":\"\","
             "\"lat\":1,\"lon\":2,\"note\":\"\",\"distance_m\":null}]}}";
    CHECK(caravan_places(&CV2) == ZXM_E_PROTOCOL && strstr(last_req, "\"method\":\"place.list\""),
          "a place with no name");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"places\":[{\"name\":\"Far\",\"lat\":1,"
             "\"lon\":2,\"note\":\"\",\"distance_m\":900},{\"name\":\"Near\",\"lat\":1,\"lon\":2,"
             "\"note\":\"\",\"distance_m\":5},{\"name\":\"Unknown\",\"lat\":1,\"lon\":2,"
             "\"note\":\"\",\"distance_m\":null}]}}";
    CHECK(caravan_places(&CV2) == ZXM_OK && CV2.nplaces == 3 &&
              !strcmp(CV2.place[0].name, "Near") && !strcmp(CV2.place[1].name, "Far") &&
              CV2.place[2].distance_m == -1,
          "nearest first");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"reset\":false}}";
    CHECK(caravan_places(&CV2) == ZXM_E_PROTOCOL && CV2.nplaces == 3, "no list: kept as it was");
    CANNED = NULL;
}
