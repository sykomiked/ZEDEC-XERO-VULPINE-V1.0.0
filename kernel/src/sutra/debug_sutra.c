#include <stdio.h>
#include "sutra.h"

int main(void) {
    sutra_token_t toks[64];
    const char *src = "IF COVERAGE-PASS THEN\n"
                       "            SET TXN-STATUS TO TRUE\n"
                       "        ELSE\n"
                       "            SET TXN-STATUS TO GLUT-MINUS\n"
                       "            INVOKE FS-PRA RESOLVE\n"
                       "        END-IF.\n";
    uint32_t n = sutra_lex_all(src, toks, 64);
    printf("token count = %u\n", n);
    for (uint32_t i = 0; i < n; i++) {
        printf("  [%u] type=%d text='%s' num=%lld den=%lld line=%u\n",
               i, toks[i].type, toks[i].text, (long long)toks[i].num, (long long)toks[i].den, toks[i].line);
    }
    return 0;
}
