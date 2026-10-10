/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_legacy_api.c — implementation of the stable C ABI. Thin wrappers over the
 * freestanding bridge modules in kernel/src/legacy. Host-side glue only; the
 * underlying modules remain freestanding and are reused unchanged. */
#include "zx_legacy_api.h"

#include "cobol.h"
#include "ebcdic.h"
#include "fortran.h"
#include "dtmf.h"
#include "tcap.h"
#include "gsm_encode.h"
#include "isup_map.h"

int32_t zx_legacy_abi_version(void)
{
    return ZX_ABI_VERSION;
}

int32_t zx_comp3_decode(const uint8_t *in, int32_t nbytes, int64_t *out)
{
    if (!in || !out || nbytes <= 0) return ZX_ERR_INVALID;
    return comp3_decode(in, (uint32_t) nbytes, out) ? ZX_OK : ZX_ERR_INVALID;
}
int32_t zx_comp3_encode(int64_t value, uint8_t *out, int32_t nbytes)
{
    if (!out || nbytes <= 0) return ZX_ERR_INVALID;
    return comp3_encode(value, out, (uint32_t) nbytes) ? ZX_OK : ZX_ERR_RANGE;
}
int32_t zx_zoned_decode(const uint8_t *in, int32_t ndigits, int64_t *out)
{
    if (!in || !out || ndigits <= 0) return ZX_ERR_INVALID;
    return zoned_decode(in, (uint32_t) ndigits, out) ? ZX_OK : ZX_ERR_INVALID;
}
int32_t zx_zoned_encode(int64_t value, uint8_t *out, int32_t ndigits, int32_t is_signed)
{
    if (!out || ndigits <= 0) return ZX_ERR_INVALID;
    return zoned_encode(value, out, (uint32_t) ndigits, is_signed != 0) ? ZX_OK : ZX_ERR_RANGE;
}
int32_t zx_comp_decode(const uint8_t *in, int32_t size, int32_t is_signed, int64_t *out)
{
    if (!in || !out) return ZX_ERR_INVALID;
    return comp_decode(in, (uint32_t) size, is_signed != 0, out) ? ZX_OK : ZX_ERR_INVALID;
}
int32_t zx_comp_encode(int64_t value, uint8_t *out, int32_t size, int32_t is_signed)
{
    if (!out) return ZX_ERR_INVALID;
    return comp_encode(value, out, (uint32_t) size, is_signed != 0) ? ZX_OK : ZX_ERR_INVALID;
}

int32_t zx_ebcdic_to_utf8(int32_t cp, const uint8_t *in, int32_t n, uint8_t *out, int32_t cap)
{
    if (!in || !out || n < 0 || cap < 0) return ZX_ERR_INVALID;
    int r = ebcdic_to_utf8((ebcdic_cp) cp, in, (uint32_t) n, out, (uint32_t) cap);
    return r < 0 ? ZX_ERR_OVERFLOW : r;
}
int32_t zx_utf8_to_ebcdic(int32_t cp, const uint8_t *in, int32_t n, uint8_t *out, int32_t cap,
                          uint8_t sub)
{
    if (!in || !out || n < 0 || cap < 0) return ZX_ERR_INVALID;
    int r = utf8_to_ebcdic((ebcdic_cp) cp, in, (uint32_t) n, out, (uint32_t) cap, sub);
    return r < 0 ? ZX_ERR_OVERFLOW : r;
}

uint32_t zx_hfp32_to_ieee32(uint32_t hfp)
{
    return hfp32_to_ieee32(hfp);
}
uint32_t zx_ieee32_to_hfp32(uint32_t ieee)
{
    return ieee32_to_hfp32(ieee);
}
uint64_t zx_hfp64_to_ieee64(uint64_t hfp)
{
    return hfp64_to_ieee64(hfp);
}
uint64_t zx_ieee64_to_hfp64(uint64_t ieee)
{
    return ieee64_to_hfp64(ieee);
}

int32_t zx_ussd_build_request(uint8_t invoke_id, const uint8_t *ascii, int32_t n, uint8_t *out,
                              int32_t cap)
{
    if (!ascii || !out || n < 0) return ZX_ERR_INVALID;
    uint8_t packed[160];
    int pn = gsm7_pack(ascii, (uint32_t) n, packed, sizeof packed);
    if (pn < 0) return ZX_ERR_OVERFLOW;
    uint8_t arg[200];
    uint32_t an = map_build_ussd_arg(0x0F, packed, (uint32_t) pn, arg, sizeof arg);
    if (an == 0) return ZX_ERR_OVERFLOW;
    map_invoke iv;
    iv.invoke_id = invoke_id;
    iv.opcode = MAP_OP_PROCESS_USS_REQ;
    for (uint32_t i = 0; i < an; i++) iv.arg[i] = arg[i];
    iv.arg_len = an;
    uint32_t cn = map_build_invoke(&iv, out, (uint32_t) cap);
    return cn == 0 ? ZX_ERR_OVERFLOW : (int32_t) cn;
}

int32_t zx_ussd_parse_request(const uint8_t *comp, int32_t len, uint8_t *ascii_out, int32_t cap,
                              int32_t *out_n)
{
    if (!comp || !ascii_out || !out_n || len < 0) return ZX_ERR_INVALID;
    map_invoke iv;
    if (!map_parse_invoke(comp, (uint32_t) len, &iv)) return ZX_ERR_INVALID;
    uint8_t dcs;
    uint8_t packed[160];
    uint32_t pn;
    if (!map_parse_ussd_arg(iv.arg, iv.arg_len, &dcs, packed, sizeof packed, &pn))
        return ZX_ERR_INVALID;
    /* septet count from packed octets: n_septets = packed*8/7 (upper bound);
     * the GSM pack is reversible given the original length, which the caller
     * recovers from the menu context. Here we unpack the maximal septet count
     * and let the caller trim on a terminator. */
    uint32_t septets = (pn * 8) / 7;
    if (septets > (uint32_t) cap) return ZX_ERR_OVERFLOW;
    int un = gsm7_unpack(packed, pn, septets, ascii_out, (uint32_t) cap);
    if (un < 0) return ZX_ERR_OVERFLOW;
    *out_n = un;
    return ZX_OK;
}

int32_t zx_dtmf_generate(uint8_t symbol, int16_t *out, int32_t nsamples, uint16_t amp,
                         uint32_t *phase_row, uint32_t *phase_col)
{
    if (!out || nsamples < 0 || !phase_row || !phase_col) return ZX_ERR_INVALID;
    return dtmf_generate((char) symbol, out, (uint32_t) nsamples, amp, phase_row, phase_col)
               ? ZX_OK
               : ZX_ERR_INVALID;
}

int32_t zx_dtmf_detect(const int16_t *pcm205)
{
    if (!pcm205) return 0;
    dtmf_detector d;
    dtmf_det_init(&d);
    return (int32_t) (unsigned char) dtmf_detect_block(&d, pcm205);
}

int32_t zx_q850_to_sip_status(uint8_t cause)
{
    return q850_to_sip_status(cause);
}
int32_t zx_sip_status_to_q850(uint16_t status)
{
    return sip_status_to_q850(status);
}
