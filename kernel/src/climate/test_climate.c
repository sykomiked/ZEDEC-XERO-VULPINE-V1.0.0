/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_climate.c — thermal modes, the climate shell controller, the start
 * interlock, the integer dew point, power-cap shedding and the ct_budget
 * feed, checked against the hardware specs' figures. */
#include "climate.h"
#include "climate_ct.h"
#include "climate_arb.h"
#include "../arbiter/arb_dev.h"
#include <stdio.h>
#include <string.h>

static int g_fail, g_checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(c)) {                                                                                \
            g_fail++;                                                                              \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                     \
        }                                                                                          \
    } while (0)

/* Reference tables, generated off-line with 50-digit decimal arithmetic:
 * magnus = the Magnus formula (b = 17.62, c = 243.12 C) evaluated exactly,
 * its90  = the dew point solved from the ITS-90 saturation vapour pressure
 *          over water (Hardy 1998), by bisection to 1e-12 K.
 * All in milli-degrees C, rounded to nearest. k_ln: round(ln(x) * 2^30). */
static const struct {
    int32_t t, rh, magnus, its90;
} k_dp[] = {
    {-45000, 100, -79787, -80380},  {-45000, 200, -75353, -75826},  {-45000, 500, -69109, -69437},
    {-45000, 1000, -64068, -64299}, {-45000, 2000, -58725, -58872}, {-45000, 3000, -55450, -55553},
    {-45000, 4000, -53054, -53129}, {-45000, 5000, -51154, -51208}, {-45000, 6000, -49573, -49611},
    {-45000, 7000, -48215, -48241}, {-45000, 8000, -47024, -47040}, {-45000, 9000, -45961, -45968},
    {-45000, 9500, -45469, -45472}, {-45000, 9900, -45092, -45093}, {-45000, 10000, -45000, -45000},
    {-40000, 100, -76404, -76925},  {-40000, 200, -71782, -72192},  {-40000, 500, -65264, -65542},
    {-40000, 1000, -59994, -60186}, {-40000, 2000, -54402, -54520}, {-40000, 3000, -50969, -51051},
    {-40000, 4000, -48457, -48516}, {-40000, 5000, -46463, -46505}, {-40000, 6000, -44803, -44833},
    {-40000, 7000, -43378, -43398}, {-40000, 8000, -42127, -42138}, {-40000, 9000, -41010, -41015},
    {-40000, 9500, -40493, -40495}, {-40000, 9900, -40097, -40097}, {-40000, 10000, -40000, -40000},
    {-35000, 100, -73050, -73504},  {-35000, 200, -68238, -68588},  {-35000, 500, -61442, -61672},
    {-35000, 1000, -55939, -56094}, {-35000, 2000, -50093, -50185}, {-35000, 3000, -46501, -46562},
    {-35000, 4000, -43870, -43912}, {-35000, 5000, -41780, -41810}, {-35000, 6000, -40040, -40061},
    {-35000, 7000, -38545, -38558}, {-35000, 8000, -37232, -37240}, {-35000, 9000, -36060, -36063},
    {-35000, 9500, -35517, -35519}, {-35000, 9900, -35102, -35102}, {-35000, 10000, -35000, -35000},
    {-30000, 100, -69726, -70114},  {-30000, 200, -64721, -65014},  {-30000, 500, -57643, -57829},
    {-30000, 1000, -51905, -52024}, {-30000, 2000, -45800, -45866}, {-30000, 3000, -42044, -42086},
    {-30000, 4000, -39292, -39320}, {-30000, 5000, -37104, -37123}, {-30000, 6000, -35282, -35295},
    {-30000, 7000, -33716, -33724}, {-30000, 8000, -32340, -32344}, {-30000, 9000, -31111, -31113},
    {-30000, 9500, -30542, -30543}, {-30000, 9900, -30107, -30107}, {-30000, 10000, -30000, -30000},
    {-25000, 100, -66431, -66758},  {-25000, 200, -61231, -61470},  {-25000, 500, -53868, -54011},
    {-25000, 1000, -47889, -47975}, {-25000, 2000, -41521, -41564}, {-25000, 3000, -37599, -37624},
    {-25000, 4000, -34723, -34738}, {-25000, 5000, -32436, -32445}, {-25000, 6000, -30529, -30535},
    {-25000, 7000, -28891, -28894}, {-25000, 8000, -27450, -27452}, {-25000, 9000, -26164, -26164},
    {-25000, 9500, -25568, -25568}, {-25000, 9900, -25112, -25112}, {-25000, 10000, -25000, -25000},
    {-20000, 100, -63164, -63433},  {-20000, 200, -57767, -57956},  {-20000, 500, -50115, -50219},
    {-20000, 1000, -43893, -43949}, {-20000, 2000, -37257, -37279}, {-20000, 3000, -33166, -33176},
    {-20000, 4000, -30163, -30167}, {-20000, 5000, -27775, -27775}, {-20000, 6000, -25783, -25782},
    {-20000, 7000, -24069, -24068}, {-20000, 8000, -22563, -22562}, {-20000, 9000, -21218, -21217},
    {-20000, 9500, -20595, -20594}, {-20000, 9900, -20117, -20117}, {-20000, 10000, -20000, -20000},
    {-15000, 100, -59926, -60141},  {-15000, 200, -54329, -54473},  {-15000, 500, -46385, -46454},
    {-15000, 1000, -39916, -39945}, {-15000, 2000, -33008, -33012}, {-15000, 3000, -28744, -28741},
    {-15000, 4000, -25613, -25606}, {-15000, 5000, -23121, -23113}, {-15000, 6000, -21041, -21035},
    {-15000, 7000, -19252, -19247}, {-15000, 8000, -17679, -17675}, {-15000, 9000, -16273, -16271},
    {-15000, 9500, -15621, -15620}, {-15000, 9900, -15122, -15122}, {-15000, 10000, -15000, -15000},
    {-10000, 100, -56715, -56881},  {-10000, 200, -50918, -51019},  {-10000, 500, -42677, -42714},
    {-10000, 1000, -35958, -35964}, {-10000, 2000, -28773, -28762}, {-10000, 3000, -24335, -24319},
    {-10000, 4000, -21072, -21057}, {-10000, 5000, -18474, -18461}, {-10000, 6000, -16305, -16294},
    {-10000, 7000, -14439, -14431}, {-10000, 8000, -12797, -12791}, {-10000, 9000, -11329, -11326},
    {-10000, 9500, -10649, -10648}, {-10000, 9900, -10127, -10127}, {-10000, 10000, -10000, -10000},
    {-5000, 100, -53532, -53652},   {-5000, 200, -47532, -47595},   {-5000, 500, -38992, -39001},
    {-5000, 1000, -32019, -32004},  {-5000, 2000, -24554, -24528},  {-5000, 3000, -19936, -19912},
    {-5000, 4000, -16540, -16519},  {-5000, 5000, -13834, -13816},  {-5000, 6000, -11575, -11561},
    {-5000, 7000, -9629, -9619},    {-5000, 8000, -7917, -7911},    {-5000, 9000, -6386, -6383},
    {-5000, 9500, -5677, -5675},    {-5000, 9900, -5133, -5133},    {-5000, 10000, -5000, -5000},
    {0, 100, -50376, -50456},       {0, 200, -44171, -44201},       {0, 500, -35328, -35313},
    {0, 1000, -28099, -28067},      {0, 2000, -20348, -20313},      {0, 3000, -15550, -15518},
    {0, 4000, -12018, -11991},      {0, 5000, -9202, -9181},        {0, 6000, -6850, -6833},
    {0, 7000, -4824, -4812},        {0, 8000, -3040, -3033},        {0, 9000, -1445, -1441},
    {0, 9500, -706, -704},          {0, 9900, -139, -138},          {0, 10000, 0, 0},
    {5000, 100, -47246, -47291},    {5000, 200, -40835, -40836},    {5000, 500, -31687, -31652},
    {5000, 1000, -24197, -24152},   {5000, 2000, -16157, -16114},   {5000, 3000, -11175, -11138},
    {5000, 4000, -7505, -7475},     {5000, 5000, -4577, -4554},     {5000, 6000, -2130, -2113},
    {5000, 7000, -22, -10},         {5000, 8000, 1834, 1842},       {5000, 9000, 3495, 3499},
    {5000, 9500, 4265, 4267},       {5000, 9900, 4856, 4856},       {5000, 10000, 5000, 5000},
    {10000, 100, -44144, -44157},   {10000, 200, -37524, -37501},   {10000, 500, -28067, -28016},
    {10000, 1000, -20314, -20259},  {10000, 2000, -11981, -11933},  {10000, 3000, -6811, -6773},
    {10000, 4000, -3001, -2970},    {10000, 5000, 41, 64},          {10000, 6000, 2584, 2601},
    {10000, 7000, 4776, 4788},      {10000, 8000, 6706, 6714},      {10000, 9000, 8434, 8437},
    {10000, 9500, 9235, 9237},      {10000, 9900, 9850, 9850},      {10000, 10000, 10000, 10000},
    {15000, 100, -41067, -41055},   {15000, 200, -34238, -34195},   {15000, 500, -24468, -24407},
    {15000, 1000, -16449, -16388},  {15000, 2000, -7819, -7770},    {15000, 3000, -2459, -2421},
    {15000, 4000, 1495, 1524},      {15000, 5000, 4652, 4673},      {15000, 6000, 7292, 7308},
    {15000, 7000, 9569, 9580},      {15000, 8000, 11575, 11582},    {15000, 9000, 13372, 13375},
    {15000, 9500, 14205, 14206},    {15000, 9900, 14844, 14844},    {15000, 10000, 15000, 15000},
    {20000, 100, -38016, -37983},   {20000, 200, -30975, -30918},   {20000, 500, -20891, -20823},
    {20000, 1000, -12602, -12540},  {20000, 2000, -3671, -3624},    {20000, 3000, 1882, 1917},
    {20000, 4000, 5980, 6006},      {20000, 5000, 9255, 9274},      {20000, 6000, 11995, 12008},
    {20000, 7000, 14359, 14368},    {20000, 8000, 16442, 16447},    {20000, 9000, 18308, 18310},
    {20000, 9500, 19174, 19175},    {20000, 9900, 19838, 19838},    {20000, 10000, 20000, 20000},
    {25000, 100, -34990, -34943},   {25000, 200, -27737, -27671},   {25000, 500, -17335, -17265},
    {25000, 1000, -8774, -8713},    {25000, 2000, 463, 504},        {25000, 3000, 6211, 6240},
    {25000, 4000, 10457, 10477},    {25000, 5000, 13852, 13865},    {25000, 6000, 16693, 16702},
    {25000, 7000, 19145, 19150},    {25000, 8000, 21307, 21310},    {25000, 9000, 23243, 23245},
    {25000, 9500, 24142, 24142},    {25000, 9900, 24831, 24832},    {25000, 10000, 25000, 25000},
    {30000, 100, -31990, -31933},   {30000, 200, -24522, -24452},   {30000, 500, -13800, -13732},
    {30000, 1000, -4963, -4909},    {30000, 2000, 4583, 4615},      {30000, 3000, 10529, 10550},
    {30000, 4000, 14925, 14937},    {30000, 5000, 18441, 18447},    {30000, 6000, 21385, 21389},
    {30000, 7000, 23927, 23928},    {30000, 8000, 26169, 26169},    {30000, 9000, 28178, 28177},
    {30000, 9500, 29110, 29109},    {30000, 9900, 29825, 29825},    {30000, 10000, 30000, 30000},
    {35000, 100, -29015, -28953},   {35000, 200, -21331, -21262},   {35000, 500, -10285, -10225},
    {35000, 1000, -1170, -1128},    {35000, 2000, 8688, 8709},      {35000, 3000, 14836, 14845},
    {35000, 4000, 19384, 19386},    {35000, 5000, 23023, 23021},    {35000, 6000, 26072, 26068},
    {35000, 7000, 28705, 28701},    {35000, 8000, 31028, 31025},    {35000, 9000, 33110, 33108},
    {35000, 9500, 34077, 34076},    {35000, 9900, 34819, 34818},    {35000, 10000, 35000, 35000},
    {40000, 100, -26064, -26003},   {40000, 200, -18163, -18100},   {40000, 500, -6791, -6744},
    {40000, 1000, 2605, 2632},      {40000, 2000, 12780, 12785},    {40000, 3000, 19132, 19126},
    {40000, 4000, 23834, 23823},    {40000, 5000, 27598, 27585},    {40000, 6000, 30754, 30741},
    {40000, 7000, 33480, 33469},    {40000, 8000, 35885, 35878},    {40000, 9000, 38042, 38038},
    {40000, 9500, 39043, 39041},    {40000, 9900, 39812, 39812},    {40000, 10000, 40000, 40000},
    {45000, 100, -23137, -23082},   {45000, 200, -15018, -14967},   {45000, 500, -3317, -3287},
    {45000, 1000, 6363, 6370},      {45000, 2000, 16858, 16843},    {45000, 3000, 23417, 23393},
    {45000, 4000, 28274, 28248},    {45000, 5000, 32166, 32141},    {45000, 6000, 35430, 35407},
    {45000, 7000, 38250, 38232},    {45000, 8000, 40740, 40727},    {45000, 9000, 42973, 42966},
    {45000, 9500, 44009, 44006},    {45000, 9900, 44805, 44805},    {45000, 10000, 45000, 45000},
    {50000, 100, -20234, -20192},   {50000, 200, -11895, -11861},   {50000, 500, 136, 144},
    {50000, 1000, 10103, 10086},    {50000, 2000, 20922, 20884},    {50000, 3000, 27690, 27646},
    {50000, 4000, 32706, 32663},    {50000, 5000, 36727, 36688},    {50000, 6000, 40100, 40067},
    {50000, 7000, 43017, 42990},    {50000, 8000, 45592, 45574},    {50000, 9000, 47902, 47893},
    {50000, 9500, 48975, 48970},    {50000, 9900, 49799, 49798},    {50000, 10000, 50000, 50000},
    {55000, 100, -17355, -17330},   {55000, 200, -8795, -8784},     {55000, 500, 3570, 3550},
    {55000, 1000, 13826, 13781},    {55000, 2000, 24972, 24908},    {55000, 3000, 31952, 31885},
    {55000, 4000, 37129, 37066},    {55000, 5000, 41281, 41225},    {55000, 6000, 44766, 44719},
    {55000, 7000, 47779, 47743},    {55000, 8000, 50441, 50417},    {55000, 9000, 52830, 52818},
    {55000, 9500, 53940, 53933},    {55000, 9900, 54792, 54790},    {55000, 10000, 55000, 55000},
    {60000, 100, -14499, -14497},   {60000, 200, -5717, -5733},     {60000, 500, 6984, 6932},
    {60000, 1000, 17532, 17454},    {60000, 2000, 29009, 28915},    {60000, 3000, 36204, 36110},
    {60000, 4000, 41543, 41458},    {60000, 5000, 45828, 45754},    {60000, 6000, 49426, 49365},
    {60000, 7000, 52538, 52492},    {60000, 8000, 55288, 55257},    {60000, 9000, 57757, 57741},
    {60000, 9500, 58904, 58896},    {60000, 9900, 59785, 59783},    {60000, 10000, 60000, 60000},
    {-1234, 4567, -11485, -11462},  {12345, 3333, -3364, -3329},    {27000, 6000, 18571, 18577},
    {18500, 7777, 14544, 14550},    {59999, 10000, 59999, 59999},   {-44999, 1, -104182, -105760},
    {33300, 1234, 419, 460},        {7700, 9999, 7699, 7699},       {1, 5000, -9201, -9180},
    {-1, 5000, -9203, -9182},
};
static const struct {
    uint32_t x;
    int64_t q30;
} k_ln[] = {
    {1u, 0LL},
    {2u, 744261118LL},
    {3u, 1179625963LL},
    {7u, 2089405113LL},
    {10u, 2472381918LL},
    {99u, 4933972369LL},
    {100u, 4944763835LL},
    {1000u, 7417145753LL},
    {4567u, 9048005538LL},
    {9999u, 9889420291LL},
    {10000u, 9889527671LL},
    {65535u, 11908161503LL},
    {1000000u, 14834291506LL},
    {2147483647u, 23072094656LL},
    {4294967295u, 23816355774LL},
};

#define N_DP ((int) (sizeof k_dp / sizeof k_dp[0]))
#define N_LN ((int) (sizeof k_ln / sizeof k_ln[0]))

static int64_t iabs64(int64_t v)
{
    return v < 0 ? -v : v;
}

static const cl_profile_t *P(const char *slug)
{
    const cl_profile_t *p = cl_profile_find(slug);
    if (!p) {
        printf("missing profile %s\n", slug);
        g_fail++;
    }
    return p;
}

static cl_input_t bc_in(void)
{
    cl_input_t in;
    memset(&in, 0, sizeof in);
    in.die_mc = CL_C(60);
    in.intake_mc = CL_C(25);
    in.exhaust_mc = CL_C(35);
    in.ambient_mc = CL_C(25);
    in.power_mw = CL_W(30);
    in.pref = CL_PREF_BALANCED;
    in.lfp_mc = CL_NO_READING;
    in.cell_mc = CL_NO_READING;
    return in;
}

/* A warm, dry server shell that passes the interlock. */
static cl_input_t sv_in(void)
{
    cl_input_t in;
    memset(&in, 0, sizeof in);
    in.outside_mc = CL_C(10);
    in.supply_mc = CL_C(22);
    in.internal_mc = CL_C(22);
    in.internal_rh_x100 = CL_RH(40);
    in.n_hdd = 6;
    in.n_ssd = 2;
    in.n_surface = 4;
    for (int i = 0; i < 6; i++) in.hdd_mc[i] = CL_C(25);
    for (int i = 0; i < 2; i++) in.ssd_mc[i] = CL_C(25);
    for (int i = 0; i < 4; i++) in.surface_mc[i] = CL_C(22);
    in.lfp_mc = CL_C(20);
    in.power_mw = CL_W(200);
    in.die_mc = in.intake_mc = in.exhaust_mc = in.ambient_mc = CL_NO_READING;
    in.cell_mc = CL_NO_READING;
    return in;
}

static cl_input_t hh_in(void)
{
    cl_input_t in;
    memset(&in, 0, sizeof in);
    in.cell_mc = CL_C(20);
    in.power_mw = CL_W(3);
    in.die_mc = in.intake_mc = in.exhaust_mc = in.ambient_mc = CL_NO_READING;
    in.lfp_mc = CL_NO_READING;
    return in;
}

static cl_output_t run(cl_ctl_t *c, const cl_input_t *in)
{
    cl_output_t o;
    memset(&o, 0xA5, sizeof o);
    bool ok = cl_step(c, in, &o);
    CHECK(ok);
    return o;
}

static void test_ln(void)
{
    for (int i = 0; i < N_LN; i++)
        CHECK(iabs64(cl_ln_q30(k_ln[i].x) - k_ln[i].q30) <= 16); /* 2^-26 in Q30 */
    CHECK(cl_ln_q30(1) == 0);
    CHECK(cl_ln_q30(0) == 0);
    CHECK(iabs64(cl_ln_q30(2) - 744261118LL) <= 1);
    int64_t prev = cl_ln_q30(1);
    for (uint32_t x = 2; x <= 20000u; x += 7) {
        int64_t v = cl_ln_q30(x);
        CHECK(v > prev);
        prev = v;
    }
    /* ln(a b) = ln a + ln b */
    for (uint32_t a = 2; a < 300; a += 13)
        for (uint32_t b = 3; b < 300; b += 17)
            CHECK(iabs64(cl_ln_q30(a * b) - cl_ln_q30(a) - cl_ln_q30(b)) <= 48);
}

static void test_dewpoint(void)
{
    int32_t td;
    for (int i = 0; i < N_DP; i++) {
        td = 12345678;
        CHECK(cl_dewpoint_mc(k_dp[i].t, (uint32_t) k_dp[i].rh, &td));
        /* integer evaluation within 10 mK of the exact Magnus value */
        CHECK(iabs64((int64_t) td - k_dp[i].magnus) <= 10);
        /* Magnus vs ITS-90 (+10 mK): 0.1 K from 0 C up and 0.6 K from -45 C
         * for RH >= 1 %; 1.6 K at 0.01 % RH (dew points near -105 C) */
        int64_t tol = k_dp[i].rh < 100 ? 1610 : k_dp[i].t >= 0 ? 110 : 610;
        CHECK(iabs64((int64_t) td - k_dp[i].its90) <= tol);
        if (k_dp[i].rh == 10000) CHECK(iabs64((int64_t) td - k_dp[i].t) <= 2);
    }
    /* textbook point: 20 C, 50 % RH -> 9.26 C (Magnus 9.255 C) */
    CHECK(cl_dewpoint_mc(CL_C(20), CL_RH(50), &td) && td >= 9245 && td <= 9265);
    /* the shell's limit: 27 C at 60 % RH -> about 18.6 C */
    CHECK(cl_dewpoint_mc(CL_C(27), CL_RH(60), &td) && td > CL_C(18) && td < CL_C(19));
    /* range and argument checks */
    td = 777;
    CHECK(!cl_dewpoint_mc(CL_C(-45) - 1, CL_RH(50), &td) && td == 777);
    CHECK(!cl_dewpoint_mc(CL_C(60) + 1, CL_RH(50), &td) && td == 777);
    CHECK(!cl_dewpoint_mc(CL_C(20), 0, &td) && td == 777);
    CHECK(!cl_dewpoint_mc(CL_C(20), 10001, &td) && td == 777);
    CHECK(!cl_dewpoint_mc(CL_C(20), CL_RH(50), NULL));
    CHECK(cl_dewpoint_mc(CL_C(-45), 1, &td) && td < CL_C(-70));
    CHECK(cl_dewpoint_mc(CL_C(60), 10000, &td) && iabs64(td - CL_C(60)) <= 2);
    /* monotonic in RH and in T; never above T (+ rounding) */
    for (int32_t t = CL_C(-45); t <= CL_C(60); t += 1000) {
        int32_t last = INT32_MIN;
        for (uint32_t rh = 100; rh <= 10000u; rh += 100) {
            CHECK(cl_dewpoint_mc(t, rh, &td));
            CHECK(td > last);
            CHECK(td <= t + 2);
            last = td;
        }
    }
    for (uint32_t rh = 500; rh <= 10000u; rh += 500) {
        int32_t last = INT32_MIN;
        for (int32_t t = CL_C(-45); t <= CL_C(60); t += 500) {
            CHECK(cl_dewpoint_mc(t, rh, &td));
            CHECK(td > last);
            last = td;
        }
    }
}

static void test_profiles(void)
{
    CHECK(cl_profile_count() == 15);
    CHECK(cl_profile(cl_profile_count()) == NULL);
    CHECK(cl_profile_find(NULL) == NULL);
    CHECK(cl_profile_find("nonesuch") == NULL);
    CHECK(cl_profile_find("") == NULL);
    CHECK(cl_profile_find("pioneerx") == NULL);
    CHECK(cl_profile_find("pione") == NULL);
    int n[4] = {0};
    for (uint32_t i = 0; i < cl_profile_count(); i++) {
        const cl_profile_t *p = cl_profile(i);
        cl_ctl_t c;
        CHECK(p != NULL);
        CHECK(cl_init(&c, p));
        CHECK(c.p == p && c.mode == CL_MODE_NONE && c.ticks == 0 && !c.rails_on);
        CHECK(cl_profile_find(p->slug) == p);
        CHECK(p->name && p->name[0]);
        CHECK(p->cls >= CL_CLASS_BRIEFCASE && p->cls <= CL_CLASS_HANDHELD);
        n[p->cls]++;
        CHECK(p->sustained_mw > 0 && p->sustained_mw <= p->peak_mw);
        for (uint32_t j = 0; j < i; j++) CHECK(strcmp(p->slug, cl_profile(j)->slug) != 0);
        if (p->cls == CL_CLASS_BRIEFCASE) {
            CHECK(p->rail_peak_mw[CL_RAIL_PROJECTOR] == CL_W(90)); /* PLATFORM_COMMON s4 */
            CHECK(p->rail_peak_mw[CL_RAIL_IT] == 0);
            CHECK(p->scram_dt_mc == CL_C(45) && p->scram_ambient_mc == CL_C(48));
            CHECK(p->prefs & (1u << CL_PREF_BALANCED));
        } else if (p->cls == CL_CLASS_SERVER) {
            CHECK(p->rail_peak_mw[CL_RAIL_IT] == p->peak_mw);
            CHECK(p->rail_peak_mw[CL_RAIL_CPU] == 0 && p->rail_peak_mw[CL_RAIL_NEURAL] == 0);
            CHECK(p->free_cool_max_mc == CL_C(15) && p->supply_max_mc == CL_C(27));
            CHECK(p->dp_max_mc == CL_C(15) && p->rh_max_x100 == CL_RH(60));
            CHECK(p->outside_min_mc == CL_C(-40) && p->outside_max_mc == CL_C(50));
        } else {
            CHECK(p->charge_min_mc == 0 && p->charge_max_mc == CL_C(45));
            CHECK(p->op_min_mc == CL_C(-30) && p->cold_throttle_mc == CL_C(-20));
            CHECK(p->rail_peak_mw[CL_RAIL_CPU] <= p->sustained_mw);
        }
    }
    CHECK(n[CL_CLASS_BRIEFCASE] == 4 && n[CL_CLASS_SERVER] == 6 && n[CL_CLASS_HANDHELD] == 5);

    /* OEM_CONSUMER s4 */
    const cl_profile_t *pi = P("pioneer");
    CHECK(pi->pref_cap_mw[CL_PREF_QUIET] == CL_W(45));
    CHECK(pi->pref_cap_mw[CL_PREF_BALANCED] == CL_W(85));
    CHECK(pi->pref_cap_mw[CL_PREF_PERFORMANCE] == CL_W(120));
    CHECK(pi->scram_die_mc == CL_C(95) && pi->jet_fault_scrams);
    CHECK(pi->silent_ceiling_mw == CL_W(45) && pi->rail_peak_mw[CL_RAIL_NEURAL] == 0);
    CHECK(pi->peak_mw == CL_W(230) && pi->sustained_mw == CL_W(150));
    /* OEM_INSTITUTIONAL s4 ratings and s5 all-on totals (360, 465, 740 W) */
    static const struct {
        const char *s;
        uint32_t sus, pk, cpu, neu, total;
    } inst[] = {{"vanguard", 250, 340, 155, 70, 360},
                {"sovereign", 330, 450, 155, 130, 465},
                {"citadel", 500, 650, 200, 260, 740}};
    for (int i = 0; i < 3; i++) {
        const cl_profile_t *p = P(inst[i].s);
        CHECK(p->sustained_mw == CL_W(inst[i].sus) && p->peak_mw == CL_W(inst[i].pk));
        CHECK(p->rail_peak_mw[CL_RAIL_CPU] == CL_W(inst[i].cpu));
        CHECK(p->rail_peak_mw[CL_RAIL_NEURAL] == CL_W(inst[i].neu));
        CHECK(p->rail_peak_mw[CL_RAIL_CPU] + p->rail_peak_mw[CL_RAIL_NEURAL] + p->other_mw ==
              CL_W(inst[i].total));
        CHECK(p->scram_die_mc == CL_C(88) && !p->jet_fault_scrams);
        CHECK(p->prefs == (1u << CL_PREF_BALANCED));
    }
    /* SERVER_COMMON s2, s4 */
    static const struct {
        const char *s;
        uint32_t sus, pk, shells, heater, comp;
        bool hdd;
    } sv[] = {
        {"hearth", 260, 360, 1, 100, 1, true},         {"homestead", 450, 650, 1, 400, 2, true},
        {"guildhall", 800, 1050, 1, 800, 2, true},     {"strongbox", 750, 1100, 1, 800, 2, false},
        {"count_house", 2600, 3700, 1, 800, 2, false}, {"hoard", 5500, 7300, 2, 800, 2, false}};
    for (int i = 0; i < 6; i++) {
        const cl_profile_t *p = P(sv[i].s);
        CHECK(p->cls == CL_CLASS_SERVER);
        CHECK(p->sustained_mw == CL_W(sv[i].sus) && p->peak_mw == CL_W(sv[i].pk));
        CHECK(p->shells == sv[i].shells && p->heater_mw == CL_W(sv[i].heater));
        CHECK(p->compressors == sv[i].comp && p->hdd_fitted == sv[i].hdd);
    }
    /* MOBILE_DEVICES s1-3, HARDENING s5 */
    static const struct {
        const char *s;
        uint32_t dev, proc;
        int32_t opmax;
    } hh[] = {{"signet", 6000, 5000, 60},
              {"folio", 12000, 10000, 60},
              {"courier_open", 15000, 10000, 55},
              {"courier_riscv", 15000, 10000, 55},
              {"courier_perf", 45000, 45000, 55}};
    for (int i = 0; i < 5; i++) {
        const cl_profile_t *p = P(hh[i].s);
        CHECK(p->cls == CL_CLASS_HANDHELD);
        CHECK(p->sustained_mw == hh[i].dev && p->rail_peak_mw[CL_RAIL_CPU] == hh[i].proc);
        CHECK(p->op_max_mc == CL_C(hh[i].opmax));
    }

    /* inconsistent profiles are refused */
    cl_ctl_t c;
    cl_profile_t bad;
    CHECK(!cl_init(NULL, pi));
    CHECK(!cl_init(&c, NULL));
    bad = *pi;
    bad.sustained_mw = bad.peak_mw + 1;
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.sustained_mw = 0;
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.slug = NULL;
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.prefs = 1u << CL_PREF_QUIET;
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.prefs |= 8u;
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.pref_cap_mw[CL_PREF_PERFORMANCE] = bad.rail_peak_mw[CL_RAIL_CPU] + 1;
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.pref_cap_mw[CL_PREF_QUIET] = 0;
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.hot_permille = 1001;
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.scram_die_mc = CL_C(50);
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.rail_peak_mw[CL_RAIL_IT] = 1;
    CHECK(!cl_init(&c, &bad));
    bad = *pi;
    bad.cls = (cl_class_t) 9;
    CHECK(!cl_init(&c, &bad));
    bad = *P("homestead");
    bad.compressors = 3;
    CHECK(!cl_init(&c, &bad));
    bad = *P("homestead");
    bad.compressors = 0;
    CHECK(!cl_init(&c, &bad));
    bad = *P("homestead");
    bad.heater_mw = 0;
    CHECK(!cl_init(&c, &bad));
    bad = *P("homestead");
    bad.rh_max_x100 = 10001;
    CHECK(!cl_init(&c, &bad));
    bad = *P("homestead");
    bad.free_cool_max_mc = CL_C(60);
    CHECK(!cl_init(&c, &bad));
    bad = *P("signet");
    bad.rail_peak_mw[CL_RAIL_CPU] = bad.sustained_mw + 1;
    CHECK(!cl_init(&c, &bad));
    bad = *P("signet");
    bad.charge_max_mc = CL_C(2);
    CHECK(!cl_init(&c, &bad));
    bad = *P("signet");
    bad.cold_throttle_mc = CL_C(5);
    CHECK(!cl_init(&c, &bad));
    bad = *P("signet");
    bad.cold_permille = 1001;
    CHECK(!cl_init(&c, &bad));
}

static void test_mode_names(void)
{
    for (int m = 1; m < CL_MODE__COUNT; m++) {
        const char *s = cl_mode_name((cl_mode_t) m);
        CHECK(s && s[0] && strcmp(s, "?") != 0);
        for (int k = 1; k < m; k++) CHECK(strcmp(s, cl_mode_name((cl_mode_t) k)) != 0);
    }
    CHECK(strcmp(cl_mode_name(CL_MODE_NONE), "?") == 0);
    CHECK(strcmp(cl_mode_name(CL_MODE__COUNT), "?") == 0);
    CHECK(strcmp(cl_mode_name(CL_MODE_SCRAM), "Scram") == 0);
    CHECK(strcmp(cl_mode_name(CL_MODE_QUIET), "Quiet") == 0);
    CHECK(strcmp(cl_mode_name(CL_MODE_LIMP_HOME), "Limp home") == 0);
}

static void test_bad_args(void)
{
    cl_ctl_t c;
    cl_output_t o;
    cl_input_t in = bc_in();
    CHECK(cl_init(&c, P("pioneer")));
    CHECK(!cl_step(NULL, &in, &o));
    CHECK(!cl_step(&c, NULL, &o));
    CHECK(!cl_step(&c, &in, NULL));
    in.faults = 0x10;
    CHECK(!cl_step(&c, &in, &o));
    in = bc_in();
    in.pref = CL_PREF__COUNT;
    CHECK(!cl_step(&c, &in, &o));
    in = bc_in();
    in.n_surface = CL_MAX_SENSORS + 1;
    CHECK(!cl_step(&c, &in, &o));
    CHECK(c.ticks == 0 && c.mode == CL_MODE_NONE);
    cl_ctl_t z;
    memset(&z, 0, sizeof z);
    in = bc_in();
    CHECK(!cl_step(&z, &in, &o));
    CHECK(!cl_service_clear(NULL, 0));
    CHECK(!cl_service_clear(&z, 0));
    cl_power_down(NULL);
    CHECK(cl_interlock(NULL, &in, NULL) == CL_IL_BAD_INPUT);
    CHECK(cl_interlock(P("hearth"), NULL, NULL) == CL_IL_BAD_INPUT);
}

/* OEM_CONSUMER s4: Quiet, Balanced, Performance */
static void test_briefcase_prefs(void)
{
    cl_ctl_t c;
    cl_output_t o;
    cl_input_t in = bc_in();
    CHECK(cl_init(&c, P("pioneer")));

    in.pref = CL_PREF_QUIET;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_QUIET);
    CHECK(o.fan_permille == 0 && o.jets_on); /* 0 rpm, micro-jets only */
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(45));
    CHECK(o.cap_mw[CL_RAIL_NEURAL] == 0 && o.cap_mw[CL_RAIL_PROJECTOR] == 0);
    CHECK(o.cap_mw[CL_RAIL_IT] == 0 && !o.rails_on && !o.heater_on && !o.compressor_on);
    CHECK(o.scram_causes == 0 && o.flags == 0 && o.interlock == 0);
    CHECK(o.dewpoint_mc == CL_NO_READING);
    CHECK(o.total_cap_mw == CL_W(45));
    CHECK(c.transitions == 1 && c.ticks == 1);
    /* Quiet holds 0 rpm even at high package power: the cap is the control */
    in.power_mw = CL_W(80);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_QUIET && o.fan_permille == 0);

    in.pref = CL_PREF_BALANCED;
    in.power_mw = CL_W(30);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_BALANCED);
    CHECK(o.fan_permille == 0 && o.jets_on); /* under the silent ceiling */
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(85));
    CHECK(c.transitions == 2);
    in.power_mw = CL_W(45);
    o = run(&c, &in);
    CHECK(o.fan_permille == 0); /* at the ceiling: still silent */
    in.power_mw = CL_W(45) + 1;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_BALANCED && o.fan_permille == 464); /* 250 + 750 * 10 / 35 */
    /* the curve: 250 at <= 50 C, full at 85 C (10 K under the 95 C scram) */
    in.die_mc = CL_C(50);
    CHECK(run(&c, &in).fan_permille == 250);
    in.die_mc = CL_C(85);
    CHECK(run(&c, &in).fan_permille == 1000);
    in.die_mc = CL_C(85) - 1;
    CHECK(run(&c, &in).fan_permille == 999);
    in.die_mc = 67500;
    CHECK(run(&c, &in).fan_permille == 625);
    in.die_mc = CL_C(40);
    CHECK(run(&c, &in).fan_permille == 250);
    in.die_mc = CL_C(60);

    in.pref = CL_PREF_PERFORMANCE;
    in.power_mw = CL_W(110);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_PERFORMANCE);
    CHECK(o.fan_permille >= 700 && o.jets_on);
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(120));
    in.die_mc = CL_C(50);
    CHECK(run(&c, &in).fan_permille == 700);
    in.die_mc = CL_C(90);
    CHECK(run(&c, &in).fan_permille == 1000);

    /* institutional tiers offer Balanced only: other preferences fall back */
    CHECK(cl_init(&c, P("vanguard")));
    in = bc_in();
    in.pref = CL_PREF_QUIET;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_BALANCED && o.cap_mw[CL_RAIL_CPU] == CL_W(155));
    in.pref = CL_PREF_PERFORMANCE;
    CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
    in.power_mw = CL_W(40) + 1; /* above the 40 W default silent ceiling */
    o = run(&c, &in);
    CHECK(o.fan_permille > 0);
}

/* PLATFORM_COMMON s6.3 / OEM_CONSUMER s4 scram triggers, each with its
 * hysteresis, and the latch until the cause clears. */
static void test_scram(void)
{
    cl_ctl_t c;
    cl_output_t o;
    cl_input_t in;
    const char *who[] = {"pioneer", "vanguard", "sovereign", "citadel"};
    for (int w = 0; w < 4; w++) {
        const cl_profile_t *p = P(who[w]);
        int32_t trig = p->scram_die_mc;
        CHECK(cl_init(&c, p));
        in = bc_in();
        /* die: "> trigger" */
        in.die_mc = trig;
        o = run(&c, &in);
        CHECK(o.mode == CL_MODE_BALANCED && o.scram_causes == 0);
        in.die_mc = trig + 1;
        o = run(&c, &in);
        CHECK(o.mode == CL_MODE_SCRAM && o.scram_causes == CL_SC_DIE);
        CHECK(o.fan_permille == 1000 && o.jets_on);
        CHECK(o.flags & CL_O_THROTTLED);
        in.die_mc = trig - CL_HYST_TEMP_MC + 1; /* inside the band: latched */
        o = run(&c, &in);
        CHECK(o.mode == CL_MODE_SCRAM && o.scram_causes == CL_SC_DIE);
        in.die_mc = trig;
        CHECK(run(&c, &in).mode == CL_MODE_SCRAM);
        in.die_mc = trig - CL_HYST_TEMP_MC; /* cause cleared */
        o = run(&c, &in);
        CHECK(o.mode == CL_MODE_BALANCED && o.scram_causes == 0);
        CHECK(!(o.flags & CL_O_THROTTLED));
        in.die_mc = trig;
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED); /* re-arms at the trigger */
        in.die_mc = CL_C(60);

        /* exhaust minus intake > 45 K */
        in.intake_mc = CL_C(20);
        in.exhaust_mc = CL_C(65);
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
        in.exhaust_mc = CL_C(65) + 1;
        o = run(&c, &in);
        CHECK(o.mode == CL_MODE_SCRAM && o.scram_causes == CL_SC_DELTA_T);
        in.exhaust_mc = CL_C(63) + 1;
        CHECK(run(&c, &in).scram_causes == CL_SC_DELTA_T);
        in.exhaust_mc = CL_C(63);
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
        in.intake_mc = CL_C(25);
        in.exhaust_mc = CL_C(35);

        /* ambient > 48 C */
        in.ambient_mc = CL_C(48);
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
        in.ambient_mc = CL_C(48) + 1;
        o = run(&c, &in);
        CHECK(o.mode == CL_MODE_SCRAM && o.scram_causes == CL_SC_AMBIENT);
        in.ambient_mc = CL_C(45) + 1;
        CHECK(run(&c, &in).mode == CL_MODE_SCRAM);
        in.ambient_mc = CL_C(45);
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED);

        /* several causes: scram holds until the last one clears */
        in.die_mc = trig + 1;
        in.ambient_mc = CL_C(50);
        o = run(&c, &in);
        CHECK(o.scram_causes == (CL_SC_DIE | CL_SC_AMBIENT));
        in.die_mc = CL_C(60);
        o = run(&c, &in);
        CHECK(o.mode == CL_MODE_SCRAM && o.scram_causes == CL_SC_AMBIENT);
        in.ambient_mc = CL_C(25);
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED);

        /* a missing reading is a scram cause */
        in.die_mc = CL_NO_READING;
        o = run(&c, &in);
        CHECK(o.mode == CL_MODE_SCRAM && (o.scram_causes & CL_SC_DIE));
        in.die_mc = CL_C(60);
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
        in.exhaust_mc = CL_NO_READING;
        CHECK(run(&c, &in).scram_causes == CL_SC_DELTA_T);
        in.exhaust_mc = CL_C(35);
        in.intake_mc = CL_NO_READING;
        CHECK(run(&c, &in).scram_causes == CL_SC_DELTA_T);
        in.intake_mc = CL_C(25);
        in.ambient_mc = CL_NO_READING;
        CHECK(run(&c, &in).scram_causes == CL_SC_AMBIENT);
        in.ambient_mc = CL_C(25);
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
        /* extreme readings do not overflow the difference */
        in.intake_mc = INT32_MIN + 1;
        in.exhaust_mc = INT32_MAX;
        CHECK(run(&c, &in).scram_causes == CL_SC_DELTA_T);
        in.intake_mc = INT32_MAX;
        in.exhaust_mc = INT32_MIN + 1;
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
    }
    /* the Pioneer's 95 C trigger: 88.001 C scrams the institutional tiers only */
    CHECK(cl_init(&c, P("pioneer")));
    in = bc_in();
    in.die_mc = CL_C(88) + 1;
    CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
    CHECK(cl_init(&c, P("citadel")));
    CHECK(run(&c, &in).mode == CL_MODE_SCRAM);

    /* the owner cannot override a scram; manual is blowers at maximum */
    CHECK(cl_init(&c, P("pioneer")));
    in = bc_in();
    in.manual = true;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_MANUAL && o.fan_permille == 1000);
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(85)); /* the preference's cap stays */
    in.pref = CL_PREF_QUIET;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_MANUAL && o.fan_permille == 1000 && o.cap_mw[CL_RAIL_CPU] == CL_W(45));
    in.die_mc = CL_C(96);
    CHECK(run(&c, &in).mode == CL_MODE_SCRAM);
    in.die_mc = CL_C(60);
    CHECK(run(&c, &in).mode == CL_MODE_MANUAL);
    in.manual = false;
    CHECK(run(&c, &in).mode == CL_MODE_QUIET);
}

/* Micro-jet faults: Pioneer scrams (OEM_CONSUMER s4); limp home until
 * service (PLATFORM_COMMON s6.3). */
static void test_jet_fault(void)
{
    cl_ctl_t c;
    cl_output_t o;
    cl_input_t in = bc_in();
    CHECK(cl_init(&c, P("pioneer")));
    CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
    in.faults = CL_F_JET_OPEN;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_SCRAM && o.scram_causes == CL_SC_JET);
    CHECK(o.fan_permille == 1000 && !o.jets_on);
    CHECK(!cl_service_clear(&c, in.faults)); /* service with the fault present */
    CHECK(run(&c, &in).mode == CL_MODE_SCRAM);
    in.faults = 0; /* the cause clears: limp home remains until service */
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_LIMP_HOME && o.fan_permille == 500 && o.jets_on);
    CHECK(o.scram_causes == 0 && o.cap_mw[CL_RAIL_CPU] == CL_W(85));
    for (int i = 0; i < 20; i++) CHECK(run(&c, &in).mode == CL_MODE_LIMP_HOME);
    in.manual = true;
    CHECK(run(&c, &in).mode == CL_MODE_MANUAL);
    in.manual = false;
    in.pref = CL_PREF_PERFORMANCE;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_LIMP_HOME && o.cap_mw[CL_RAIL_CPU] == CL_W(85));
    CHECK(cl_service_clear(&c, 0));
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_PERFORMANCE && o.cap_mw[CL_RAIL_CPU] == CL_W(120));
    in.faults = CL_F_JET_OVERCURR;
    CHECK(run(&c, &in).scram_causes == CL_SC_JET);

    /* institutional: limp home directly, no scram */
    CHECK(cl_init(&c, P("sovereign")));
    in = bc_in();
    in.faults = CL_F_JET_OVERCURR;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_LIMP_HOME && o.scram_causes == 0);
    CHECK(o.fan_permille == 500 && !o.jets_on);
    in.faults = 0;
    CHECK(run(&c, &in).mode == CL_MODE_LIMP_HOME);
    in.die_mc = CL_C(90);
    CHECK(run(&c, &in).mode == CL_MODE_SCRAM); /* scram outranks limp home */
    in.die_mc = CL_C(60);
    CHECK(run(&c, &in).mode == CL_MODE_LIMP_HOME);
    CHECK(cl_service_clear(&c, 0));
    CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
    /* compressor fault bits do not touch a briefcase */
    in.faults = CL_F_COMPRESSOR_A | CL_F_COMPRESSOR_B;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_BALANCED && o.jets_on);
}

/* Hysteresis: readings dithering across a trigger do not chatter. */
static void test_hysteresis(void)
{
    cl_ctl_t c;
    cl_output_t o;
    cl_input_t in = bc_in();
    CHECK(cl_init(&c, P("pioneer")));
    run(&c, &in);
    uint32_t t0 = c.transitions;
    for (int i = 0; i < 200; i++) {
        in.die_mc = (i & 1) ? CL_C(93) : CL_C(95) + 1;
        o = run(&c, &in);
        CHECK(o.mode == CL_MODE_SCRAM);
    }
    CHECK(c.transitions == t0 + 1);
    /* just under the trigger, dithering: never enters */
    CHECK(cl_init(&c, P("pioneer")));
    for (int i = 0; i < 200; i++) {
        in.die_mc = (i & 1) ? CL_C(95) : CL_C(80);
        CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
    }
    CHECK(c.transitions == 1);
    /* ambient and delta-T */
    in.die_mc = CL_C(60);
    for (int i = 0; i < 100; i++) {
        in.ambient_mc = (i & 1) ? CL_C(46) : CL_C(48) + 1;
        CHECK(run(&c, &in).mode == CL_MODE_SCRAM);
    }
    in.ambient_mc = CL_C(25);
    CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
    in.intake_mc = CL_C(20);
    for (int i = 0; i < 100; i++) {
        in.exhaust_mc = (i & 1) ? CL_C(64) : CL_C(65) + 1;
        CHECK(run(&c, &in).mode == CL_MODE_SCRAM);
    }
    in.exhaust_mc = CL_C(30);
    CHECK(run(&c, &in).mode == CL_MODE_BALANCED);
    CHECK(c.transitions == 5);
    /* blowers around the silent ceiling (45 W, off at 40 W) */
    in.die_mc = CL_C(50);
    for (int i = 0; i < 100; i++) {
        in.power_mw = (i & 1) ? CL_W(41) : CL_W(45) + 1;
        CHECK(run(&c, &in).fan_permille == 250);
    }
    in.power_mw = CL_W(40);
    CHECK(run(&c, &in).fan_permille == 0);
    in.power_mw = CL_W(44);
    CHECK(run(&c, &in).fan_permille == 0);
}

/* OEM_INSTITUTIONAL s5: caps hold the rating; projector sheds neural. */
static void test_power_caps(void)
{
    cl_ctl_t c;
    cl_output_t o;
    cl_input_t in = bc_in();

    CHECK(cl_init(&c, P("citadel")));
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(200));
    CHECK(o.cap_mw[CL_RAIL_NEURAL] == CL_W(170)); /* 740 W all-on held at 650 W */
    CHECK(o.cap_mw[CL_RAIL_PROJECTOR] == 0);
    CHECK(o.total_cap_mw == CL_W(650));
    CHECK(!(o.flags & (CL_O_PROJECTOR_SHED | CL_O_THROTTLED)));
    uint32_t neural_off = o.cap_mw[CL_RAIL_NEURAL];
    in.hatch_open = true;
    in.projector_req = true;
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_PROJECTOR] == CL_W(90));
    CHECK(neural_off - o.cap_mw[CL_RAIL_NEURAL] == CL_W(90)); /* same amount */
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(200));
    CHECK(o.total_cap_mw == CL_W(650));
    CHECK(o.flags & CL_O_PROJECTOR_SHED);
    CHECK(o.flags & CL_O_THROTTLED);
    /* the hatch interlock: no projector power with the hatch closed */
    in.hatch_open = false;
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_PROJECTOR] == 0 && o.cap_mw[CL_RAIL_NEURAL] == CL_W(170));
    in.hatch_open = true;
    in.projector_req = false;
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_PROJECTOR] == 0 && o.cap_mw[CL_RAIL_NEURAL] == CL_W(170));
    /* scram: hot-ambient caps (50 % of rail peak, default) */
    in.projector_req = true;
    in.die_mc = CL_C(89);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_SCRAM);
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(100) && o.cap_mw[CL_RAIL_NEURAL] == CL_W(80));
    in.projector_req = false;
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(100) && o.cap_mw[CL_RAIL_NEURAL] == CL_W(130));

    CHECK(cl_init(&c, P("sovereign")));
    in = bc_in();
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(155) && o.cap_mw[CL_RAIL_NEURAL] == CL_W(115));
    CHECK(o.total_cap_mw == CL_W(450));
    in.hatch_open = in.projector_req = true;
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_NEURAL] == CL_W(25) && o.cap_mw[CL_RAIL_CPU] == CL_W(155));
    CHECK(o.total_cap_mw == CL_W(450));

    /* Vanguard: neural (50 W after the rating) cannot give 90 W; the
     * remaining 40 W comes from the CPU */
    CHECK(cl_init(&c, P("vanguard")));
    in = bc_in();
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(155) && o.cap_mw[CL_RAIL_NEURAL] == CL_W(50));
    CHECK(o.total_cap_mw == CL_W(340));
    in.hatch_open = in.projector_req = true;
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_NEURAL] == 0 && o.cap_mw[CL_RAIL_CPU] == CL_W(115));
    CHECK(o.cap_mw[CL_RAIL_PROJECTOR] == CL_W(90) && o.total_cap_mw == CL_W(340));

    /* Pioneer: no neural rail, the projector fits inside the 230 W supply */
    CHECK(cl_init(&c, P("pioneer")));
    in = bc_in();
    in.pref = CL_PREF_PERFORMANCE;
    in.hatch_open = in.projector_req = true;
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(120) && o.cap_mw[CL_RAIL_PROJECTOR] == CL_W(90));
    CHECK(o.cap_mw[CL_RAIL_NEURAL] == 0 && !(o.flags & CL_O_PROJECTOR_SHED));
    CHECK(o.total_cap_mw == CL_W(210));
    in.die_mc = CL_C(96);
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_CPU] == CL_W(60)); /* scram: 50 % of 120 W */

    /* invariant: every briefcase, every preference, projector on or off,
     * scram or not, stays within its rating */
    for (uint32_t i = 0; i < cl_profile_count(); i++) {
        const cl_profile_t *p = cl_profile(i);
        if (p->cls != CL_CLASS_BRIEFCASE) continue;
        for (uint32_t pref = 0; pref < CL_PREF__COUNT; pref++)
            for (int proj = 0; proj < 2; proj++)
                for (int hot = 0; hot < 2; hot++) {
                    CHECK(cl_init(&c, p));
                    in = bc_in();
                    in.pref = pref;
                    in.hatch_open = in.projector_req = proj;
                    in.die_mc = hot ? CL_C(99) : CL_C(60);
                    o = run(&c, &in);
                    CHECK(o.total_cap_mw <= p->peak_mw);
                    CHECK(o.total_cap_mw == o.cap_mw[CL_RAIL_CPU] + o.cap_mw[CL_RAIL_NEURAL] +
                                                o.cap_mw[CL_RAIL_PROJECTOR] + p->other_mw);
                    CHECK(o.cap_mw[CL_RAIL_CPU] <= p->rail_peak_mw[CL_RAIL_CPU]);
                    CHECK(o.cap_mw[CL_RAIL_NEURAL] <= p->rail_peak_mw[CL_RAIL_NEURAL]);
                    CHECK(o.cap_mw[CL_RAIL_PROJECTOR] == (proj ? CL_W(90) : 0));
                    CHECK(o.mode == (hot ? CL_MODE_SCRAM : o.mode));
                }
    }
}

/* SERVER_COMMON s2.3: each interlock condition passes and fails on its own. */
static void test_interlock(void)
{
    const cl_profile_t *hdd = P("homestead"), *flash = P("strongbox");
    cl_input_t in = sv_in();
    int32_t dp = 0;
    CHECK(cl_interlock(hdd, &in, &dp) == 0);
    CHECK(dp > CL_C(7) && dp < CL_C(9)); /* 22 C at 40 % RH: about 7.8 C */
    CHECK(cl_interlock(flash, &in, NULL) == 0);

    /* (1) hard drives >= 10 C */
    in.hdd_mc[3] = CL_C(10);
    CHECK(cl_interlock(hdd, &in, NULL) == 0);
    in.hdd_mc[3] = CL_C(10) - 1;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_HDD_COLD);
    CHECK(cl_interlock(flash, &in, NULL) == CL_IL_HDD_COLD); /* any drive fitted counts */
    in.hdd_mc[3] = CL_NO_READING;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_NO_HDD);
    in = sv_in();
    in.n_hdd = 0;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_NO_HDD);
    CHECK(cl_interlock(flash, &in, NULL) == 0); /* all-flash: no drives required */
    /* (1) all-flash: every SSD >= 0 C */
    in.ssd_mc[1] = 0;
    CHECK(cl_interlock(flash, &in, NULL) == 0);
    in.ssd_mc[1] = -1;
    CHECK(cl_interlock(flash, &in, NULL) == CL_IL_SSD_COLD);
    CHECK(cl_interlock(hdd, &in, NULL) == (CL_IL_SSD_COLD | CL_IL_NO_HDD));
    in.ssd_mc[1] = CL_NO_READING;
    CHECK(cl_interlock(flash, &in, NULL) == CL_IL_NO_SSD);
    in = sv_in();
    in.n_hdd = 0;
    in.n_ssd = 0;
    CHECK(cl_interlock(flash, &in, NULL) == CL_IL_NO_SSD);
    in = sv_in();
    in.n_ssd = 0;
    CHECK(cl_interlock(hdd, &in, NULL) == 0);

    /* (2) every surface at least 3 K above the dew point */
    in = sv_in();
    CHECK(cl_interlock(hdd, &in, &dp) == 0);
    in.surface_mc[2] = dp + CL_C(3);
    CHECK(cl_interlock(hdd, &in, NULL) == 0);
    in.surface_mc[2] = dp + CL_C(3) - 1;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_CONDENSATION);
    in.surface_mc[2] = dp;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_CONDENSATION);
    in.surface_mc[2] = CL_NO_READING;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_NO_SURFACE);
    in = sv_in();
    in.n_surface = 0;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_NO_SURFACE);
    /* a humid shell: same surfaces, higher dew point */
    in = sv_in();
    in.internal_rh_x100 = CL_RH(90); /* 22 C at 90 %: dew point about 20.3 C */
    CHECK(cl_interlock(hdd, &in, &dp) == CL_IL_CONDENSATION);
    CHECK(dp > CL_C(20) && dp < CL_C(21));
    in.internal_rh_x100 = 0;
    CHECK(cl_interlock(hdd, &in, &dp) == CL_IL_NO_DEWPOINT);
    CHECK(dp == CL_NO_READING);
    in.internal_rh_x100 = CL_RH(40);
    in.internal_mc = CL_NO_READING;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_NO_DEWPOINT);
    in.internal_mc = CL_C(61); /* outside the dew-point range */
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_NO_DEWPOINT);

    /* (3) LFP above 0 C */
    in = sv_in();
    in.lfp_mc = 1;
    CHECK(cl_interlock(hdd, &in, NULL) == 0);
    in.lfp_mc = 0;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_LFP_COLD);
    in.lfp_mc = CL_C(-10);
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_LFP_COLD);
    in.lfp_mc = CL_NO_READING;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_NO_LFP);

    /* bad counts */
    in = sv_in();
    in.n_hdd = CL_MAX_SENSORS + 1;
    CHECK(cl_interlock(hdd, &in, &dp) == CL_IL_BAD_INPUT && dp == CL_NO_READING);
    in = sv_in();
    in.n_ssd = CL_MAX_SENSORS + 1;
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_BAD_INPUT);
    /* full sensor groups are accepted */
    in = sv_in();
    in.n_hdd = in.n_ssd = in.n_surface = CL_MAX_SENSORS;
    for (uint32_t i = 0; i < CL_MAX_SENSORS; i++) {
        in.hdd_mc[i] = CL_C(20);
        in.ssd_mc[i] = CL_C(20);
        in.surface_mc[i] = CL_C(20);
    }
    CHECK(cl_interlock(hdd, &in, NULL) == 0);
    in.surface_mc[CL_MAX_SENSORS - 1] = CL_C(-5);
    CHECK(cl_interlock(hdd, &in, NULL) == CL_IL_CONDENSATION);

    /* cold soak at -40 C: everything fails at once */
    in = sv_in();
    for (int i = 0; i < 6; i++) in.hdd_mc[i] = CL_C(-40);
    for (int i = 0; i < 2; i++) in.ssd_mc[i] = CL_C(-40);
    for (int i = 0; i < 4; i++) in.surface_mc[i] = CL_C(-40);
    in.internal_mc = CL_C(-40);
    in.internal_rh_x100 = CL_RH(80);
    in.lfp_mc = CL_C(-40);
    CHECK(cl_interlock(hdd, &in, NULL) ==
          (CL_IL_HDD_COLD | CL_IL_SSD_COLD | CL_IL_CONDENSATION | CL_IL_LFP_COLD));
}

/* Cold start through the controller: heaters, rails held off, then start. */
static void test_server_start(void)
{
    cl_ctl_t c;
    cl_output_t o;
    const cl_profile_t *p = P("guildhall");
    CHECK(cl_init(&c, p));
    cl_input_t in = sv_in();
    for (int i = 0; i < 6; i++) in.hdd_mc[i] = CL_C(-40);
    for (int i = 0; i < 2; i++) in.ssd_mc[i] = CL_C(-40);
    for (int i = 0; i < 4; i++) in.surface_mc[i] = CL_C(-39);
    in.internal_mc = CL_C(-40);
    in.internal_rh_x100 = CL_RH(70);
    in.lfp_mc = CL_C(-40);
    in.outside_mc = CL_C(-40);
    in.supply_mc = CL_C(-40);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_WARMUP && !o.rails_on && o.heater_on);
    CHECK(o.cap_mw[CL_RAIL_IT] == 0 && o.total_cap_mw == 0);
    CHECK(!o.charge_enable && !o.compressor_on && o.fan_permille == 300);
    CHECK(o.interlock & CL_IL_HDD_COLD);
    /* warm in stages; each remaining condition keeps the rails off */
    for (int i = 0; i < 6; i++) in.hdd_mc[i] = CL_C(12);
    for (int i = 0; i < 2; i++) in.ssd_mc[i] = CL_C(12);
    in.internal_mc = CL_C(12);
    in.internal_rh_x100 = CL_RH(50); /* dew point about 1.9 C */
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_WARMUP && o.heater_on &&
          o.interlock == (CL_IL_CONDENSATION | CL_IL_LFP_COLD));
    for (int i = 0; i < 4; i++) in.surface_mc[i] = CL_C(5);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_WARMUP && o.interlock == CL_IL_LFP_COLD && o.heater_on);
    CHECK(!o.charge_enable);
    in.lfp_mc = 1;
    o = run(&c, &in);
    CHECK(o.interlock == 0 && o.rails_on);
    CHECK(o.mode == CL_MODE_FREE_COOLING);
    CHECK(o.cap_mw[CL_RAIL_IT] == p->peak_mw && o.total_cap_mw == p->peak_mw);
    CHECK(!o.charge_enable); /* charging waits 1 K above 0 C */
    CHECK(o.heater_on);      /* LFP below the hold band */
    /* the start interlock gates the start only: a cold drive later does not
     * drop the rails, which heat the shell themselves */
    in.hdd_mc[0] = CL_C(9);
    o = run(&c, &in);
    CHECK(o.rails_on && o.interlock == CL_IL_HDD_COLD && o.mode == CL_MODE_FREE_COOLING);
    /* power down: the next start is gated again */
    cl_power_down(&c);
    o = run(&c, &in);
    CHECK(!o.rails_on && o.mode == CL_MODE_WARMUP && o.cap_mw[CL_RAIL_IT] == 0);
    in.hdd_mc[0] = CL_C(10);
    o = run(&c, &in);
    CHECK(o.rails_on && o.mode == CL_MODE_FREE_COOLING);
}

static void test_server_regimes(void)
{
    cl_ctl_t c;
    cl_output_t o;
    CHECK(cl_init(&c, P("count_house")));
    cl_input_t in = sv_in();
    in.n_hdd = 0;
    in.outside_mc = CL_C(15) - 1;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_FREE_COOLING && !o.compressor_on && o.rails_on);
    in.outside_mc = CL_C(15);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_ACTIVE_COOLING && o.compressor_on && o.compressor_unit == 0);
    in.outside_mc = CL_C(12) + 1;
    CHECK(run(&c, &in).mode == CL_MODE_ACTIVE_COOLING);
    in.outside_mc = CL_C(12);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    /* chatter test across 15 C */
    uint32_t t0 = c.transitions;
    for (int i = 0; i < 100; i++) {
        in.outside_mc = (i & 1) ? CL_C(13) : CL_C(15);
        CHECK(run(&c, &in).mode == CL_MODE_ACTIVE_COOLING);
    }
    CHECK(c.transitions == t0 + 1);
    in.outside_mc = CL_C(10);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    /* load: supply air over 27 C starts the compressor on a cold day */
    in.supply_mc = CL_C(27);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    in.supply_mc = CL_C(27) + 1;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_ACTIVE_COOLING && o.compressor_on && o.fan_permille == 1000);
    in.supply_mc = CL_C(24) + 1;
    CHECK(run(&c, &in).mode == CL_MODE_ACTIVE_COOLING);
    in.supply_mc = CL_C(24);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_FREE_COOLING && o.fan_permille == 300 + 700 * 6 / 9);
    in.supply_mc = CL_C(18);
    CHECK(run(&c, &in).fan_permille == 300);
    in.supply_mc = CL_NO_READING;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_ACTIVE_COOLING && o.fan_permille == 1000);
    in.supply_mc = CL_C(22);
    in.outside_mc = CL_NO_READING;
    CHECK(run(&c, &in).mode == CL_MODE_ACTIVE_COOLING);
    in.outside_mc = CL_C(10);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    /* facility water: compressor not fitted */
    in.facility_water = true;
    in.outside_mc = CL_C(35);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_FACILITY_WATER && !o.compressor_on);
    in.facility_water = false;
    /* N+1: unit B takes over; both failed is reported */
    in.faults = CL_F_COMPRESSOR_A;
    o = run(&c, &in);
    CHECK(o.compressor_on && o.compressor_unit == 1 && !(o.flags & CL_O_NO_COMPRESSOR));
    in.faults = CL_F_COMPRESSOR_B;
    o = run(&c, &in);
    CHECK(o.compressor_on && o.compressor_unit == 0);
    in.faults = CL_F_COMPRESSOR_A | CL_F_COMPRESSOR_B;
    o = run(&c, &in);
    CHECK(!o.compressor_on && (o.flags & CL_O_NO_COMPRESSOR));
    in.faults = 0;
    /* the S pedestal has one unit */
    cl_ctl_t h;
    CHECK(cl_init(&h, P("hearth")));
    cl_input_t hin = sv_in();
    hin.outside_mc = CL_C(30);
    o = run(&h, &hin);
    CHECK(o.compressor_on && o.compressor_unit == 0);
    hin.faults = CL_F_COMPRESSOR_A;
    o = run(&h, &hin);
    CHECK(!o.compressor_on && (o.flags & CL_O_NO_COMPRESSOR));
    /* outside the external envelope */
    in.outside_mc = CL_C(50);
    CHECK(!(run(&c, &in).flags & CL_O_OUT_OF_ENVELOPE));
    in.outside_mc = CL_C(50) + 1;
    CHECK(run(&c, &in).flags & CL_O_OUT_OF_ENVELOPE);
    in.outside_mc = CL_C(-40);
    CHECK(!(run(&c, &in).flags & CL_O_OUT_OF_ENVELOPE));
    in.outside_mc = CL_C(-40) - 1;
    CHECK(run(&c, &in).flags & CL_O_OUT_OF_ENVELOPE);
}

/* SERVER_COMMON s1, s2.4 humidity; s4 LFP charging; s2.3 heater hold. */
static void test_server_humidity_lfp(void)
{
    cl_ctl_t c;
    cl_output_t o;
    CHECK(cl_init(&c, P("hearth")));
    cl_input_t in = sv_in();
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    in.internal_rh_x100 = CL_RH(60);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    in.internal_rh_x100 = CL_RH(60) + 1;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_DEHUMIDIFY && o.compressor_on);
    CHECK(o.cap_mw[CL_RAIL_IT] == CL_W(360)); /* humidity alone holds no cap */
    in.internal_rh_x100 = CL_RH(56);
    CHECK(run(&c, &in).mode == CL_MODE_DEHUMIDIFY);
    in.internal_rh_x100 = CL_RH(55);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    /* dew point over 15 C at a modest RH */
    in.internal_mc = CL_C(27);
    in.internal_rh_x100 = CL_RH(55); /* dew point about 17.1 C */
    for (int i = 0; i < 4; i++) in.surface_mc[i] = CL_C(27);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_DEHUMIDIFY && o.dewpoint_mc > CL_C(15));
    in.internal_mc = CL_C(22);
    in.internal_rh_x100 = CL_RH(50);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    in.internal_rh_x100 = 0; /* no dew point: dehumidify, fail closed */
    CHECK(run(&c, &in).mode == CL_MODE_DEHUMIDIFY);
    in.internal_rh_x100 = CL_RH(40);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    /* chatter across 60 % RH */
    uint32_t t0 = c.transitions;
    for (int i = 0; i < 100; i++) {
        in.internal_rh_x100 = (i & 1) ? CL_RH(58) : CL_RH(60) + 1;
        CHECK(run(&c, &in).mode == CL_MODE_DEHUMIDIFY);
    }
    CHECK(c.transitions == t0 + 1);
    in.internal_rh_x100 = CL_RH(40);
    run(&c, &in);

    /* LFP charging: never at or below 0 C, re-enabled 1 K above */
    in.lfp_mc = CL_C(1);
    CHECK(run(&c, &in).charge_enable);
    in.lfp_mc = 1;
    CHECK(run(&c, &in).charge_enable);
    in.lfp_mc = 0;
    CHECK(!run(&c, &in).charge_enable);
    in.lfp_mc = CL_C(1) - 1;
    CHECK(!run(&c, &in).charge_enable);
    in.lfp_mc = CL_C(1);
    CHECK(run(&c, &in).charge_enable);
    in.lfp_mc = CL_NO_READING;
    CHECK(!run(&c, &in).charge_enable);
    for (int pass = 0; pass < 2; pass++)
        for (int32_t t = CL_C(-5); t <= CL_C(5); t += 50) {
            in.lfp_mc = pass ? -t : t;
            o = run(&c, &in);
            CHECK(!o.charge_enable || in.lfp_mc > 0);
            if (in.lfp_mc >= CL_C(1)) CHECK(o.charge_enable);
        }
    /* heater holds the LFP above its charging limit: on at 2 C, off at 5 C */
    in.lfp_mc = CL_C(6);
    CHECK(!run(&c, &in).heater_on);
    in.lfp_mc = CL_C(2) + 1;
    CHECK(!run(&c, &in).heater_on);
    in.lfp_mc = CL_C(2);
    CHECK(run(&c, &in).heater_on);
    in.lfp_mc = CL_C(5) - 1;
    CHECK(run(&c, &in).heater_on);
    in.lfp_mc = CL_C(5);
    CHECK(!run(&c, &in).heater_on);
    in.lfp_mc = CL_C(20);
    CHECK(!run(&c, &in).heater_on);
}

/* SERVER_COMMON s2.4 door rule: cooling first, unlock, dehumidify after the
 * door closes before any power limit is raised back. */
static void test_server_service(void)
{
    cl_ctl_t c;
    cl_output_t o;
    const cl_profile_t *p = P("strongbox");
    CHECK(cl_init(&c, p));
    cl_input_t in = sv_in();
    in.n_hdd = 0;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_FREE_COOLING && !o.door_unlock);
    in.service_request = true;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_SERVICE && !o.door_unlock); /* cooling raised first */
    CHECK(o.fan_permille == 1000 && o.compressor_on);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_SERVICE && o.door_unlock);
    in.door_open = true;
    in.internal_rh_x100 = CL_RH(75); /* humid air comes in */
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_SERVICE && o.door_unlock && o.fan_permille == 1000);
    CHECK(o.cap_mw[CL_RAIL_IT] == p->peak_mw); /* held, not raised */
    in.door_open = false;
    in.service_request = false;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_DEHUMIDIFY && !o.door_unlock && o.compressor_on);
    in.internal_rh_x100 = CL_RH(57);
    CHECK(run(&c, &in).mode == CL_MODE_DEHUMIDIFY);
    in.internal_rh_x100 = CL_RH(50);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    /* a dry shell still runs the cycle for one step after the door closes */
    in.service_request = true;
    run(&c, &in);
    run(&c, &in);
    in.door_open = true;
    run(&c, &in);
    in.service_request = in.door_open = false;
    CHECK(run(&c, &in).mode == CL_MODE_DEHUMIDIFY);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    /* a service request withdrawn before the door opens: no cycle */
    in.service_request = true;
    run(&c, &in);
    in.service_request = false;
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);
    /* a door forced open without a request is a service too */
    in.door_open = true;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_SERVICE && o.fan_permille == 1000);
    in.door_open = false;
    CHECK(run(&c, &in).mode == CL_MODE_DEHUMIDIFY);
    CHECK(run(&c, &in).mode == CL_MODE_FREE_COOLING);

    /* The hold: a service that starts during warm-up holds the IT cap at
     * zero until the dehumidify cycle completes, even once the interlock
     * passes. */
    CHECK(cl_init(&c, p));
    in = sv_in();
    in.n_hdd = 0;
    in.lfp_mc = CL_C(-5);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_WARMUP && o.cap_mw[CL_RAIL_IT] == 0);
    in.service_request = true;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_SERVICE && !o.door_unlock);
    o = run(&c, &in);
    CHECK(o.door_unlock);
    in.door_open = true;
    in.lfp_mc = CL_C(10); /* the interlock now passes */
    in.internal_rh_x100 = CL_RH(70);
    o = run(&c, &in);
    CHECK(o.rails_on && o.mode == CL_MODE_SERVICE);
    CHECK(o.cap_mw[CL_RAIL_IT] == 0 && (o.flags & CL_O_THROTTLED));
    in.door_open = in.service_request = false;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_DEHUMIDIFY && o.cap_mw[CL_RAIL_IT] == 0);
    in.internal_rh_x100 = CL_RH(58);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_DEHUMIDIFY && o.cap_mw[CL_RAIL_IT] == 0);
    in.internal_rh_x100 = CL_RH(45);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_FREE_COOLING && o.cap_mw[CL_RAIL_IT] == p->peak_mw);
    CHECK(!(o.flags & CL_O_THROTTLED));
    /* facility-water shell: dehumidify relies on the desiccant, no compressor */
    in.facility_water = true;
    in.door_open = true;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_SERVICE && !o.compressor_on && o.fan_permille == 1000);
    in.door_open = false;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_DEHUMIDIFY && !o.compressor_on);
    CHECK(run(&c, &in).mode == CL_MODE_FACILITY_WATER);
}

/* HARDENING s5: handheld throttle, cell heater and charging window. */
static void test_handheld(void)
{
    cl_ctl_t c;
    cl_output_t o;
    CHECK(cl_init(&c, P("signet")));
    cl_input_t in = hh_in();
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_NORMAL && o.cap_mw[CL_RAIL_CPU] == 5000 && o.total_cap_mw == 6000);
    CHECK(!o.charge_enable && !o.heater_on && o.fan_permille == 0 && o.flags == 0);
    in.charger_present = true;
    o = run(&c, &in);
    CHECK(o.charge_enable && !o.heater_on);
    /* cold throttle below about -20 C */
    in.charger_present = false;
    in.cell_mc = CL_C(-20);
    CHECK(run(&c, &in).mode == CL_MODE_NORMAL);
    in.cell_mc = CL_C(-20) - 1;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_COLD_THROTTLE && o.cap_mw[CL_RAIL_CPU] == 2500);
    CHECK(o.flags & CL_O_THROTTLED);
    in.cell_mc = CL_C(-17) - 1;
    CHECK(run(&c, &in).mode == CL_MODE_COLD_THROTTLE);
    in.cell_mc = CL_C(-17);
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_NORMAL && o.cap_mw[CL_RAIL_CPU] == 5000);
    uint32_t t0 = c.transitions;
    for (int i = 0; i < 100; i++) {
        in.cell_mc = (i & 1) ? CL_C(-18) : CL_C(-20) - 1;
        CHECK(run(&c, &in).mode == CL_MODE_COLD_THROTTLE);
    }
    CHECK(c.transitions == t0 + 1);
    /* plugged in at -30 C: heat first, charge after */
    in.cell_mc = CL_C(-30);
    in.charger_present = true;
    o = run(&c, &in);
    CHECK(o.heater_on && !o.charge_enable && (o.flags & CL_O_COLD_CELL));
    in.cell_mc = -1;
    o = run(&c, &in);
    CHECK(o.heater_on && !o.charge_enable);
    in.cell_mc = 0;
    o = run(&c, &in);
    CHECK(o.heater_on && !o.charge_enable); /* heater runs on to 1 C */
    in.cell_mc = CL_C(1);
    o = run(&c, &in);
    CHECK(!o.heater_on && o.charge_enable);
    in.cell_mc = 0;
    o = run(&c, &in);
    CHECK(!o.heater_on && o.charge_enable); /* 0 C is inside the window */
    in.cell_mc = -1;
    o = run(&c, &in);
    CHECK(o.heater_on && !o.charge_enable);
    /* no charger: no heater (it is powered from the charger), no charging */
    in.charger_present = false;
    o = run(&c, &in);
    CHECK(!o.heater_on && !o.charge_enable);
    /* hot side: off above 45 C, back at 42 C */
    in.charger_present = true;
    in.cell_mc = CL_C(45);
    CHECK(run(&c, &in).charge_enable);
    in.cell_mc = CL_C(45) + 1;
    CHECK(!run(&c, &in).charge_enable);
    in.cell_mc = CL_C(42) + 1;
    CHECK(!run(&c, &in).charge_enable);
    in.cell_mc = CL_C(42);
    CHECK(run(&c, &in).charge_enable);
    /* sweeps: charging only inside 0..45 C */
    for (int pass = 0; pass < 2; pass++)
        for (int32_t t = CL_C(-30); t <= CL_C(60); t += 250) {
            in.cell_mc = pass ? CL_C(30) - t : t;
            o = run(&c, &in);
            CHECK(!o.charge_enable || (in.cell_mc >= 0 && in.cell_mc <= CL_C(45)));
            CHECK(!o.heater_on || in.cell_mc < CL_C(1));
        }
    /* operating envelope */
    in.cell_mc = CL_C(60);
    CHECK(!(run(&c, &in).flags & CL_O_OUT_OF_ENVELOPE));
    in.cell_mc = CL_C(60) + 1;
    CHECK(run(&c, &in).flags & CL_O_OUT_OF_ENVELOPE);
    in.cell_mc = CL_C(-30) - 1;
    CHECK(run(&c, &in).flags & CL_O_OUT_OF_ENVELOPE);
    /* missing cell reading: throttle, never charge or heat */
    in.cell_mc = CL_NO_READING;
    o = run(&c, &in);
    CHECK(o.mode == CL_MODE_COLD_THROTTLE && !o.charge_enable && !o.heater_on);
    /* the laptop's +55 C limit and the performance board's 45 W */
    CHECK(cl_init(&c, P("courier_perf")));
    in = hh_in();
    in.cell_mc = CL_C(55) + 1;
    o = run(&c, &in);
    CHECK((o.flags & CL_O_OUT_OF_ENVELOPE) && o.cap_mw[CL_RAIL_CPU] == 45000);
    CHECK(o.total_cap_mw == 45000);
    in.cell_mc = CL_C(-25);
    CHECK(run(&c, &in).cap_mw[CL_RAIL_CPU] == 22500);
    CHECK(cl_init(&c, P("folio")));
    in = hh_in();
    o = run(&c, &in);
    CHECK(o.cap_mw[CL_RAIL_CPU] == 10000 && o.total_cap_mw == 12000);
}

/* The ct_budget feed (cotier/ct_budget.h B2). */
static void test_ct(void)
{
    cl_ctl_t c;
    ct_metrics_t m;
    ct_budget_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    memset(&m, 0, sizeof m);
    CHECK(cl_init(&c, P("pioneer")));
    CHECK(!cl_ct_metrics(&c, &m) && m.present == 0); /* no sample yet */
    CHECK(!cl_ct_refs(&c, &cfg) && cfg.watts_ref_mw == 0);
    CHECK(!cl_ct_metrics(NULL, &m));
    CHECK(!cl_ct_metrics(&c, NULL));
    CHECK(!cl_ct_refs(NULL, &cfg));
    cl_input_t in = bc_in();
    in.power_mw = CL_W(85);
    run(&c, &in);
    CHECK(!cl_ct_refs(&c, NULL));
    CHECK(cl_ct_metrics(&c, &m));
    CHECK(m.present == (CT_M_WATTS | CT_M_TEMP));
    CHECK(m.watts_mw == CL_W(85) && m.headroom_mc == CL_C(35));
    CHECK(cl_ct_refs(&c, &cfg));
    CHECK(cfg.watts_ref_mw == CL_W(85) && cfg.headroom_ref_mc == CL_CT_HEADROOM_REF_MC);
    cfg.w_watts = ZT_ONE;
    cfg.w_temp = ZT_ONE;
    CHECK(ct_cost(&cfg, &m) == ZT_ONE); /* at the cap, cool: watts term only */
    in.die_mc = CL_C(90);               /* 5 K below the 95 C trigger */
    run(&c, &in);
    memset(&m, 0, sizeof m);
    CHECK(cl_ct_metrics(&c, &m) && m.headroom_mc == CL_C(5));
    CHECK(ct_cost(&cfg, &m) == ZT_ONE + ZT_ONE / 2);
    in.die_mc = CL_C(96); /* past the trigger: scram, full thermal cost */
    run(&c, &in);
    memset(&m, 0, sizeof m);
    CHECK(cl_ct_metrics(&c, &m) && m.headroom_mc == -CL_C(1));
    CHECK(cl_ct_refs(&c, &cfg) && cfg.watts_ref_mw == CL_W(60)); /* scram cap */
    CHECK(ct_cost(&cfg, &m) == 92842 + ZT_ONE); /* 85 W of a 60 W cap, + full thermal */
    /* the metrics callback drives ct_budget_step: its cost is ct_cost of
     * the same sample */
    ct_budget_cfg_t bc;
    memset(&bc, 0, sizeof bc);
    bc.eta = ZT_ONE;
    bc.delta = ZT_ONE / 20;
    bc.N = 16;
    bc.w_watts = ZT_ONE;
    bc.w_temp = ZT_ONE;
    bc.h_low = ZT_ONE / 4;
    bc.h_high = ZT_ONE / 2;
    bc.hold = 4;
    bc.levels = 4;
    bc.batch_max = 64;
    bc.batch_min = 1;
    bc.ctx_max = 4096;
    bc.ctx_min = 512;
    in.die_mc = CL_C(60);
    run(&c, &in);
    CHECK(cl_ct_refs(&c, &bc));
    ct_budget_t b;
    CHECK(ct_budget_init(&b, &bc));
    ct_budget_step(&b, cl_ct_metrics, &c, 0);
    memset(&m, 0, sizeof m);
    CHECK(cl_ct_metrics(&c, &m));
    CHECK(b.C == ct_cost(&bc, &m) && b.C == ZT_ONE);
    in.die_mc = CL_C(94);
    run(&c, &in);
    ct_budget_step(&b, cl_ct_metrics, &c, 0);
    CHECK(b.C > ZT_ONE);

    /* server and handheld headroom */
    CHECK(cl_init(&c, P("hoard")));
    in = sv_in();
    in.n_hdd = 0;
    in.supply_mc = CL_C(24);
    run(&c, &in);
    memset(&m, 0, sizeof m);
    CHECK(cl_ct_metrics(&c, &m) && m.headroom_mc == CL_C(3) && m.watts_mw == CL_W(200));
    CHECK(cl_ct_refs(&c, &cfg) && cfg.watts_ref_mw == CL_W(7300));
    cl_power_down(&c);
    in.lfp_mc = CL_C(-1);
    run(&c, &in);
    CHECK(!cl_ct_refs(&c, &cfg)); /* rails off: no compute cap to refer to */
    CHECK(cl_init(&c, P("signet")));
    in = hh_in();
    in.cell_mc = CL_C(35);
    run(&c, &in);
    memset(&m, 0, sizeof m);
    CHECK(cl_ct_metrics(&c, &m) && m.headroom_mc == CL_C(25) && m.watts_mw == CL_W(3));
    CHECK(cl_ct_refs(&c, &cfg) && cfg.watts_ref_mw == 5000);
}

/* Arbiter telemetry into climate inputs, through the real mailbox: the
 * arbiter's end pushes readings, the host's end polls them, the adapter
 * copies only what was reported and refuses impossible power. Then the
 * sample drives a step and ct_budget sees the result. */
static uint8_t g_win[0x4000] __attribute__((aligned(64)));

static void zero_entropy(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    memset(out, 0, n);
}

static void test_arb_inputs(void)
{
    arb_dev_t d;
    arb_host_t h;
    uint8_t meas[ARB_MEAS_LEN] = {0};
    memset(g_win, 0, sizeof g_win);
    memset(&d, 0, sizeof d);
    memset(&h, 0, sizeof h);
    CHECK(arb_dev_init(&d, g_win, sizeof g_win, 8, 0, 0, 0, meas, zero_entropy, 0) == 0);
    CHECK(arb_host_init(&h, g_win, sizeof g_win) == 0);

    cl_input_t in;
    memset(&in, 0, sizeof in);
    in.die_mc = 1234;
    uint32_t bad = 99;
    /* nothing reported yet: nothing written, nothing invented */
    CHECK(cl_arb_inputs(&h, &in, &bad) == 0 && bad == 0 && in.die_mc == 1234);

    uint16_t s1[] = {ARB_S_T_CPU,      ARB_S_T_NEURAL, ARB_S_PWR_CPU,
                     ARB_S_PWR_NEURAL, ARB_S_T_INTAKE, ARB_S_T_EXHAUST};
    int32_t v1[] = {CL_C(71), CL_C(84), 38000, 61000, CL_C(30), CL_C(52)};
    CHECK(arb_dev_telemetry(&d, s1, v1, 6) == 0);
    arb_host_poll(&h);
    uint32_t got = cl_arb_inputs(&h, &in, &bad);
    CHECK(got == (CL_ARB_DIE | CL_ARB_INTAKE | CL_ARB_EXHAUST | CL_ARB_POWER) && bad == 0);
    CHECK(in.die_mc == CL_C(84)); /* the hotter die */
    CHECK(in.intake_mc == CL_C(30) && in.exhaust_mc == CL_C(52));
    CHECK(in.power_mw == 99000); /* CPU + neural only */
    CHECK(in.cell_mc == 0);      /* never reported: untouched */

    /* the CPU die runs hotter later */
    uint16_t s2[] = {ARB_S_T_CPU, ARB_S_T_CELL};
    int32_t v2[] = {CL_C(90), CL_C(-5)};
    CHECK(arb_dev_telemetry(&d, s2, v2, 2) == 0);
    arb_host_poll(&h);
    got = cl_arb_inputs(&h, &in, NULL);
    CHECK((got & CL_ARB_CELL) && in.cell_mc == CL_C(-5));
    CHECK(in.die_mc == CL_C(90));

    /* negative power is impossible: refused, the last good value kept */
    uint16_t s3[] = {ARB_S_PWR_NEURAL};
    int32_t v3[] = {-1};
    CHECK(arb_dev_telemetry(&d, s3, v3, 1) == 0);
    arb_host_poll(&h);
    got = cl_arb_inputs(&h, &in, &bad);
    CHECK(!(got & CL_ARB_POWER) && bad == CL_ARB_POWER && in.power_mw == 99000);
    CHECK(got & CL_ARB_DIE);

    /* the largest readings still sum inside 32 bits */
    int32_t v4[] = {0x7FFFFFFF, 0x7FFFFFFF};
    uint16_t s4[] = {ARB_S_PWR_CPU, ARB_S_PWR_NEURAL};
    CHECK(arb_dev_telemetry(&d, s4, v4, 2) == 0);
    arb_host_poll(&h);
    CHECK(cl_arb_inputs(&h, &in, &bad) && bad == 0 && in.power_mw == 0xFFFFFFFEu);

    /* only one die sensor reported: that one is the die */
    arb_host_t h2;
    memset(&h2, 0, sizeof h2);
    h2.sensor[ARB_S_T_NEURAL] = CL_C(66);
    h2.sensor_seen = 1u << ARB_S_T_NEURAL;
    memset(&in, 0, sizeof in);
    CHECK(cl_arb_inputs(&h2, &in, &bad) == CL_ARB_DIE && in.die_mc == CL_C(66));
    h2.sensor[ARB_S_PWR_CPU] = 5000;
    h2.sensor_seen |= 1u << ARB_S_PWR_CPU;
    CHECK(cl_arb_inputs(&h2, &in, &bad) == (CL_ARB_DIE | CL_ARB_POWER) && in.power_mw == 5000);
    h2.sensor[ARB_S_PWR_CPU] = -1; /* the CPU rail alone, impossible */
    CHECK(cl_arb_inputs(&h2, &in, &bad) == CL_ARB_DIE && bad == CL_ARB_POWER);
    CHECK(in.power_mw == 5000);
    h2.sensor[ARB_S_PWR_CPU] = 5000;

    /* the chain: arbiter readings -> cl_step -> ct_budget metrics */
    cl_ctl_t c;
    cl_output_t o;
    ct_metrics_t m;
    memset(&in, 0, sizeof in);
    CHECK(cl_init(&c, P("pioneer")));
    CHECK(cl_arb_inputs(&h2, &in, NULL) & CL_ARB_POWER);
    in.ambient_mc = CL_C(25);
    in.intake_mc = CL_C(25);
    in.exhaust_mc = CL_C(35);
    CHECK(cl_step(&c, &in, &o));
    memset(&m, 0, sizeof m);
    CHECK(cl_ct_metrics(&c, &m));
    CHECK((m.present & CT_M_WATTS) && m.watts_mw == 5000);
}

int main(void)
{
    test_ln();
    test_dewpoint();
    test_profiles();
    test_mode_names();
    test_bad_args();
    test_briefcase_prefs();
    test_scram();
    test_jet_fault();
    test_hysteresis();
    test_power_caps();
    test_interlock();
    test_server_start();
    test_server_regimes();
    test_server_humidity_lfp();
    test_server_service();
    test_handheld();
    test_ct();
    test_arb_inputs();
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
