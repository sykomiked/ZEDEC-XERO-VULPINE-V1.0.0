#include "sutra.h"

static int starts_with(const char *a, const char *b) {
    while (*b != '\0') {
        if (*a++ != *b++) return 0;
    }
    return 1;
}

int32_t sutra_emit_message(const vino_transaction_t *txn, const char *emit_form, const char *subtype, char *out, uint32_t max_out) {
    if (starts_with(emit_form, "EMIT-ISO20022")) {
        if (starts_with(subtype, "PACS")) {
            return vino_msg_to_pacs008(txn, out, max_out);
        } else if (starts_with(subtype, "CAMT")) {
            return vino_msg_to_camt053(txn, out, max_out);
        } else {
            return vino_msg_to_iso20022(txn, out, max_out);
        }
    } else if (starts_with(emit_form, "EMIT-SWIFT")) {
        if (starts_with(subtype, "MT103")) {
            return vino_msg_to_mt103(txn, out, max_out);
        } else {
            return vino_msg_to_mt103(txn, out, max_out);
        }
    } else {
        return -1;
    }
}