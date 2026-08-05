/* test_lightningrod_regress.c — regressions for the 2026-08-04 self-audit.
 * width+1 wrapped to 0 and defeated the bounds check (stack overflow). */
#include <stdio.h>
#include <string.h>
#include "lightningrod.h"
static int F=0;
#define CK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);F++;} else printf("[PASS] %s\n",m);}while(0)
int main(void){
    uint8_t in[8]="ABCD    "; char out[8]; memset(out,0x7E,sizeof out);
    lr_result_t r = lr_fixed_to_cstr(in, 0xFFFFFFFFu, out, 8);
    CK(!r.ok, "width=UINT32_MAX is REFUSED (was a stack overflow)");
    CK(out[7]==0x7E, "the output buffer was not touched");
    r = lr_fixed_to_cstr(in, 4, out, 8);
    CK(r.ok && !strcmp(out,"ABCD"), "a normal call still works");
    r = lr_fixed_to_cstr(in, 8, out, 8);
    CK(!r.ok, "width == max is refused (no room for NUL)");
    rat_t v;
    CK(!lr_packed_to_rat((const uint8_t*)"\x12\x34\x5C",3,19,&v).ok, "packed scale>18 refused");
    CK(!lr_zoned_to_rat((const uint8_t*)"12345",5,19,&v).ok, "zoned scale>18 refused");
    CK(lr_packed_to_rat((const uint8_t*)"\x12\x34\x5C",3,2,&v).ok, "packed scale=2 still works");

    /* #17: INT64_MIN refused at the adapter boundary */
    {
        rat_t v;
        CK(!lr_scaled_to_rat((-9223372036854775807LL-1), 2, &v).ok,
           "lr_scaled_to_rat(INT64_MIN) refused");
    }
    /* #18: exactly-representable small doubles now convert */
    {
        rat_t v;
        /* 2^-60: bits = (1023-60)<<52 */
        uint64_t bits = ((uint64_t)(1023-60)) << 52;
        CK(lr_double_bits_to_rat(bits, &v).ok,
           "2^-60 converts (guard now tests the REDUCED exponent)");
        CK(v.valid && v.den == (1ll<<60) && v.num == 1, "and equals 1/2^60 exactly");
        CK(!lr_double_bits_to_rat(((uint64_t)(1023-80))<<52, &v).ok,
           "2^-80 still refuses (genuinely unrepresentable)");
    }
    printf("\n%s: %d failure(s)\n", F?"*** FAILED ***":"ALL PASS", F);
    return F?1:0;
}
