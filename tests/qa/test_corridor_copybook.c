/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cobol.h"

int main(int argc, char **argv)
{
    char text[4096];
    FILE *file = fopen("bindings/cobol/corridor.cpy", "rb");
    assert(file);
    size_t size = fread(text, 1, sizeof(text), file);
    assert(!ferror(file) && size < sizeof(text));
    fclose(file);
    cob_layout layout;
    assert(cobol_parse_copybook(text, (uint32_t) size, &layout));
    assert(layout.nfields == 8);
    uint8_t record[128] = {0};
    assert(layout.record_size <= sizeof(record));
    const char *names[] = {"AGREEMENT-VERSION", "NODE-A-ID",         "NODE-B-ID",
                           "BASE-VBILL-ANCHOR", "COMPRESSION-KAPPA", "ASSURANCE-FEE-NUM",
                           "ASSURANCE-FEE-DEN", "COLLATERAL-LIMIT"};
    const int64_t values[] = {1, 566, 404, 1000000, 10000, 8, 9000, 500000};
    unsigned checks = 0;
    for (unsigned i = 0; i < 8; i++) {
        const cob_field *field = cobol_find(&layout, names[i]);
        assert(field && field->scale == 0 && !field->is_signed);
        assert(cobol_set_int(record, layout.record_size, field, values[i]));
        int64_t decoded = -1;
        assert(cobol_get_int(record, layout.record_size, field, &decoded));
        assert(decoded == values[i]);
        checks++;
        uint8_t saved[128];
        memcpy(saved, record, sizeof(record));
        assert(!cobol_set_int(record, layout.record_size, field, -1));
        assert(memcmp(saved, record, sizeof(record)) == 0);
        checks++;
        int64_t limit = 1;
        for (unsigned digit = 0; digit < field->ndigits; digit++) limit *= 10;
        assert(!cobol_set_int(record, layout.record_size, field, limit));
        assert(memcmp(saved, record, sizeof(record)) == 0);
        checks++;
        for (uint32_t length = 0; length < field->offset + field->size; length++) {
            decoded = 42;
            assert(!cobol_get_int(record, length, field, &decoded));
            assert(decoded == 42);
            assert(!cobol_set_int(record, length, field, values[i]));
            assert(memcmp(saved, record, sizeof(record)) == 0);
            checks++;
        }
        /* Valid packed bytes can still violate the declared unsigned PIC. */
        assert(comp3_encode(-1, record + field->offset, field->size));
        decoded = 42;
        assert(!cobol_get_int(record, layout.record_size, field, &decoded));
        assert(decoded == 42);
        if (field->ndigits % 2 == 0) {
            assert(comp3_encode(limit, record + field->offset, field->size));
            assert(!cobol_get_int(record, layout.record_size, field, &decoded));
            assert(decoded == 42);
            checks++;
        }
        checks++;
        memcpy(record, saved, sizeof(record));
    }
    if (argc > 1) {
        uint8_t compiled[128];
        file = fopen(argv[1], "rb");
        assert(file);
        size = fread(compiled, 1, sizeof(compiled), file);
        assert(!ferror(file));
        fclose(file);
        assert(size == layout.record_size);
        for (unsigned i = 0; i < 8; i++) {
            const cob_field *field = cobol_find(&layout, names[i]);
            int64_t decoded;
            assert(cobol_get_int(compiled, (uint32_t)size, field, &decoded));
            assert(decoded == values[i]);
            checks++;
        }
        assert(memcmp(compiled, record, size) == 0);
        checks++;
    }
    printf("corridor copybook: %u schema/bounds checks passed\n", checks);
    return 0;
}
