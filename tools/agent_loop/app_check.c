/* app_check.c — parse one .zxapp with the kernel's own parser and run its
 * checks, exactly as the OS does at load. A build-time validator: if this
 * passes, the OS accepts the app.
 *
 * usage: app_check <file.zxapp>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxapp.h"

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s <file.zxapp>\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t) n + 1);
    if (!text) { fclose(f); return 2; }
    if (fread(text, 1, (size_t) n, f) != (size_t) n) { fclose(f); free(text); return 2; }
    text[n] = 0;
    fclose(f);

    static zxapp_t a;
    int r = zxapp_parse(&a, text, (uint32_t) n);
    if (r != 0) {
        printf("PARSE FAILED (line %u): %s\n", a.err_line, a.err);
        return 1;
    }
    printf("parsed: id=%s name=%s matrix=%u entries=%u checks=%u\n",
           a.id, a.name, a.matrix, a.n, a.ncheck);

    static zxapp_host_t h;
    memset(&h, 0, sizeof h);
    int loaded = zxapp_load(&a, &h);
    if (loaded < 0) {
        printf("LOAD FAILED (line %u): %s\n", a.err_line, a.err);
        return 1;
    }
    printf("loaded: %d logic entries compiled\n", loaded);

    int ok = zxapp_check(&a, &h);
    if (ok != (int) a.ncheck) {
        printf("CHECK FAILED (line %u): %s\n", a.err_line, a.err);
        printf("  %d of %u checks held\n", ok < 0 ? 0 : ok, a.ncheck);
        return 1;
    }
    printf("ALL CHECKS HOLD: %d of %u\n", ok, a.ncheck);
    return 0;
}
