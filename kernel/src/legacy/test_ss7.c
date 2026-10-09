/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ss7.c — M3UA + SCCP UDT + TCAP + MAP + GSM encoding tests, including a
 * full USSD mobile-money request assembled through every layer and parsed
 * back. Host test (stdio). */
#include <stdio.h>
#include <string.h>
#include "ss7.h"
#include "tcap.h"
#include "gsm_encode.h"

static int pass = 0, fail = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            pass++;                                                                                \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)
#define OK(c) CHECK(c, #c)

static void test_gsm(void)
{
    /* Published vector: "hellohello" packed in GSM 7-bit (3GPP TS 23.038) is
     * E8 32 9B FD 46 97 D9 EC 37. */
    const uint8_t expect[] = {0xE8, 0x32, 0x9B, 0xFD, 0x46, 0x97, 0xD9, 0xEC, 0x37};
    uint8_t packed[16];
    int pn = gsm7_pack((const uint8_t *) "hellohello", 10, packed, sizeof packed);
    OK(pn == 9);
    OK(memcmp(packed, expect, 9) == 0);
    uint8_t un[16];
    int cn = gsm7_unpack(packed, (uint32_t) pn, 10, un, sizeof un);
    OK(cn == 10 && memcmp(un, "hellohello", 10) == 0);

    /* USSD menu string round-trips */
    const char *menu = "*182*1#";
    uint8_t p2[16];
    int p2n = gsm7_pack((const uint8_t *) menu, 7, p2, sizeof p2);
    OK(p2n > 0);
    uint8_t u2[16];
    OK(gsm7_unpack(p2, (uint32_t) p2n, 7, u2, sizeof u2) == 7);
    OK(memcmp(u2, menu, 7) == 0);

    /* overflow is reported */
    uint8_t tiny[1];
    OK(gsm7_pack((const uint8_t *) "hellohello", 10, tiny, 1) == -1);

    /* UCS-2 */
    uint8_t uc[32];
    int ucn = ucs2_encode((const uint8_t *) "Pay", 3, uc, sizeof uc);
    OK(ucn == 6 && uc[0] == 0 && uc[1] == 'P' && uc[4] == 0 && uc[5] == 'y');
    uint8_t back[16];
    OK(ucs2_decode(uc, 6, back, sizeof back) == 3 && memcmp(back, "Pay", 3) == 0);
}

static void test_m3ua(void)
{
    m3ua_data d = {0};
    d.opc = 0x11223344;
    d.dpc = 0x55667788;
    d.si = 3; /* SCCP */
    d.ni = 2;
    d.sls = 7;
    const char *pl = "PAYLOAD";
    memcpy(d.payload, pl, 7);
    d.payload_len = 7;
    uint8_t out[128];
    uint32_t n = m3ua_build_data(&d, out, sizeof out);
    OK(n > 0);
    OK(out[0] == 1 && out[2] == 1 && out[3] == 1);
    m3ua_data d2 = {0};
    OK(m3ua_parse_data(out, n, &d2));
    OK(d2.opc == 0x11223344 && d2.dpc == 0x55667788 && d2.si == 3 && d2.sls == 7);
    OK(d2.payload_len == 7 && memcmp(d2.payload, "PAYLOAD", 7) == 0);
}

static void test_sccp(void)
{
    sccp_udt u = {0};
    u.protocol_class = 0;
    sccp_set_gt(&u.called, 6 /*HLR SSN*/, 0, 1 /*ISDN*/, 4 /*international*/, "233540000001");
    sccp_set_gt(&u.calling, 8 /*MSC SSN*/, 0, 1, 4, "233200000002");
    const char *data = "TCAPDATA";
    memcpy(u.data, data, 8);
    u.data_len = 8;
    uint8_t out[256];
    uint32_t n = sccp_build_udt(&u, out, sizeof out);
    OK(n > 0);
    OK(out[0] == SCCP_MSG_UDT);
    sccp_udt u2 = {0};
    OK(sccp_parse_udt(out, n, &u2));
    OK(u2.called.gt.present && u2.called.ssn == 6);
    OK(u2.called.gt.ndigits == 12 && memcmp(u2.called.gt.digits, "233540000001", 12) == 0);
    OK(u2.calling.gt.ndigits == 12 && memcmp(u2.calling.gt.digits, "233200000002", 12) == 0);
    OK(u2.data_len == 8 && memcmp(u2.data, "TCAPDATA", 8) == 0);
}

static void test_tcap_map_ussd(void)
{
    /* Build USSD-Arg for "*182*1*500#" (a mobile-money transfer menu) */
    const char *ussd = "*182*1*500#";
    uint8_t packed[32];
    int pn = gsm7_pack((const uint8_t *) ussd, (uint32_t) strlen(ussd), packed, sizeof packed);
    OK(pn > 0);
    uint8_t arg[64];
    uint32_t an =
        map_build_ussd_arg(0x0F /*GSM 7-bit dcs*/, packed, (uint32_t) pn, arg, sizeof arg);
    OK(an > 0);

    /* wrap in a MAP Invoke, opcode processUnstructuredSS-Request */
    map_invoke iv = {0};
    iv.invoke_id = 1;
    iv.opcode = MAP_OP_PROCESS_USS_REQ;
    memcpy(iv.arg, arg, an);
    iv.arg_len = an;
    uint8_t comp[128];
    uint32_t cn = map_build_invoke(&iv, comp, sizeof comp);
    OK(cn > 0);

    /* wrap in TCAP Begin */
    tcap_msg t = {0};
    t.type = TCAP_BEGIN;
    t.has_otid = true;
    t.otid = 0x01020304;
    memcpy(t.comp, comp, cn);
    t.comp_len = cn;
    uint8_t tc[256];
    uint32_t tn = tcap_build(&t, tc, sizeof tc);
    OK(tn > 0);

    /* carry in SCCP UDT */
    sccp_udt u = {0};
    sccp_set_gt(&u.called, 6, 0, 1, 4, "233540000001");
    sccp_set_gt(&u.calling, 8, 0, 1, 4, "233200000002");
    memcpy(u.data, tc, tn);
    u.data_len = tn;
    uint8_t sc[512];
    uint32_t sn = sccp_build_udt(&u, sc, sizeof sc);
    OK(sn > 0);

    /* carry in M3UA DATA */
    m3ua_data d = {0};
    d.opc = 1001;
    d.dpc = 2002;
    d.si = 3;
    memcpy(d.payload, sc, sn);
    d.payload_len = sn;
    uint8_t wire[700];
    uint32_t wn = m3ua_build_data(&d, wire, sizeof wire);
    OK(wn > 0);

    /* ---- now parse the whole stack back down ---- */
    m3ua_data rd = {0};
    OK(m3ua_parse_data(wire, wn, &rd));
    OK(rd.si == 3);
    sccp_udt ru = {0};
    OK(sccp_parse_udt(rd.payload, rd.payload_len, &ru));
    OK(ru.called.gt.ndigits == 12);
    tcap_msg rt = {0};
    OK(tcap_parse(ru.data, ru.data_len, &rt));
    OK(rt.type == TCAP_BEGIN && rt.has_otid && rt.otid == 0x01020304);
    map_invoke riv = {0};
    OK(map_parse_invoke(rt.comp, rt.comp_len, &riv));
    OK(riv.invoke_id == 1 && riv.opcode == MAP_OP_PROCESS_USS_REQ);
    uint8_t dcs;
    uint8_t rstr[32];
    uint32_t rn;
    OK(map_parse_ussd_arg(riv.arg, riv.arg_len, &dcs, rstr, sizeof rstr, &rn));
    OK(dcs == 0x0F && rn == (uint32_t) pn);
    uint8_t text[32];
    int tcn = gsm7_unpack(rstr, rn, (uint32_t) strlen(ussd), text, sizeof text);
    OK(tcn == (int) strlen(ussd) && memcmp(text, ussd, tcn) == 0);
}

static void test_tcap_variants(void)
{
    /* Continue and End round-trip with dtid */
    tcap_msg t = {0};
    t.type = TCAP_END;
    t.has_dtid = true;
    t.dtid = 0xDEADBEEF;
    const char *body = "component-bytes";
    memcpy(t.comp, body, strlen(body));
    t.comp_len = (uint32_t) strlen(body);
    uint8_t out[128];
    uint32_t n = tcap_build(&t, out, sizeof out);
    OK(n > 0 && out[0] == TCAP_TAG_END);
    tcap_msg r = {0};
    OK(tcap_parse(out, n, &r));
    OK(r.type == TCAP_END && r.has_dtid && r.dtid == 0xDEADBEEF);
    OK(r.comp_len == strlen(body));

    /* forwardSM arg round-trip */
    const uint8_t tpdu[] = {0x04, 0x0B, 0x91, 0x33, 0x25, 0x00, 0x00, 0x01, 0xC8, 0x32};
    uint8_t arg[64];
    uint32_t an = map_build_forwardsm_arg(tpdu, sizeof tpdu, arg, sizeof arg);
    OK(an > 0);
    uint8_t rtp[64];
    uint32_t rn;
    OK(map_parse_forwardsm_arg(arg, an, rtp, sizeof rtp, &rn));
    OK(rn == sizeof tpdu && memcmp(rtp, tpdu, rn) == 0);
}

static void test_fuzz(void)
{
    uint32_t r = 0xABCDEF01u;
    uint8_t buf[256];
    for (int it = 0; it < 6000; it++) {
        for (int i = 0; i < 256; i++) {
            r = r * 1664525u + 1013904223u;
            buf[i] = (uint8_t) (r >> 19);
        }
        uint32_t L = r % 256;
        m3ua_data d;
        (void) m3ua_parse_data(buf, L, &d);
        sccp_udt u;
        (void) sccp_parse_udt(buf, L, &u);
        tcap_msg t;
        (void) tcap_parse(buf, L, &t);
        map_invoke iv;
        (void) map_parse_invoke(buf, L, &iv);
        uint8_t o[32], dcs, nn;
        uint32_t on;
        (void) map_parse_ussd_arg(buf, L, &dcs, o, sizeof o, &on);
        (void) nn;
    }
    OK(1);
}

int main(void)
{
    test_gsm();
    test_m3ua();
    test_sccp();
    test_tcap_map_ussd();
    test_tcap_variants();
    test_fuzz();
    printf("SS7/TCAP/MAP/GSM: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
