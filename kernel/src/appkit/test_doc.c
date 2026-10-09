/* test_doc.c — host tests for the AppKit document model.
 *
 *   gcc -std=c11 -Wall -Wextra -Isrc/appkit src/appkit/test_doc.c \
 *       src/appkit/doc.c -o /tmp/test_doc && /tmp/test_doc
 */
#include <stdio.h>
#include <string.h>
#include "doc.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

static int is(doc_t *d, const char *want) {
    char b[DOC_CAPACITY+1];
    uint32_t n = doc_text(d, b, DOC_CAPACITY); b[n] = 0;
    return strcmp(b, want) == 0;
}
static void show(doc_t *d, const char *label) {
    char b[DOC_CAPACITY+1];
    uint32_t n = doc_text(d, b, DOC_CAPACITY); b[n] = 0;
    printf("       %s: \"%s\"\n", label, b);
}

int main(void) {
    doc_t d;
    printf("=== AppKit document model ===\n");

    doc_init(&d, "untitled");
    CHECK(doc_length(&d) == 0 && !doc_is_dirty(&d), "init: empty and clean");

    /* insertion */
    CHECK(doc_insert(&d, 0, "Hello world", 11), "insert at 0");
    CHECK(is(&d, "Hello world"), "text is 'Hello world'");
    CHECK(doc_is_dirty(&d), "dirty after edit");
    CHECK(doc_length(&d) == 11, "length 11");

    /* insert in the middle (exercises gap movement) */
    doc_break_coalesce(&d);
    CHECK(doc_insert(&d, 5, ",", 1), "insert comma mid-document");
    CHECK(is(&d, "Hello, world"), "text is 'Hello, world'");

    /* insert at end */
    doc_break_coalesce(&d);
    CHECK(doc_insert(&d, doc_length(&d), "!", 1), "append '!'");
    CHECK(is(&d, "Hello, world!"), "text is 'Hello, world!'");
    show(&d, "doc");

    /* delete */
    doc_break_coalesce(&d);
    CHECK(doc_delete(&d, 5, 1), "delete the comma");
    CHECK(is(&d, "Hello world!"), "comma removed");

    /* undo restores the deletion */
    CHECK(doc_undo(&d), "undo delete");
    CHECK(is(&d, "Hello, world!"), "undo restored the comma");
    /* redo re-applies it */
    CHECK(doc_redo(&d), "redo delete");
    CHECK(is(&d, "Hello world!"), "redo removed it again");

    /* undo of an insert */
    CHECK(doc_undo(&d), "undo (back to comma present)");
    CHECK(doc_undo(&d), "undo the '!' insert");
    CHECK(is(&d, "Hello, world"), "'!' undone");
    show(&d, "doc");

    /* --- coalescing: a typing run is ONE undo step --- */
    doc_t t; doc_init(&t, "typing");
    const char *word = "dragon";
    for (int i = 0; i < 6; i++) doc_insert(&t, doc_length(&t), &word[i], 1);
    CHECK(is(&t, "dragon"), "typed 'dragon' one char at a time");
    CHECK(t.undo_count == 1, "6 keystrokes coalesced into 1 undo record");
    CHECK(doc_undo(&t), "undo the typing run");
    CHECK(is(&t, ""), "one undo removed the whole run");
    CHECK(doc_redo(&t) && is(&t, "dragon"), "redo restores the run");

    /* a break splits runs */
    doc_t s; doc_init(&s, "split");
    doc_insert(&s, 0, "ab", 2);
    doc_break_coalesce(&s);
    doc_insert(&s, 2, "cd", 2);
    CHECK(s.undo_count == 2, "break_coalesce splits into 2 undo records");
    doc_undo(&s);
    CHECK(is(&s, "ab"), "undo removed only the second run");

    /* new edit invalidates redo */
    doc_t r; doc_init(&r, "redo");
    doc_insert(&r, 0, "xyz", 3);
    doc_undo(&r);
    CHECK(r.redo_count == 1, "redo available after undo");
    doc_insert(&r, 0, "Q", 1);
    CHECK(r.redo_count == 0, "new edit clears the redo stack");

    /* capacity is enforced, never truncated */
    doc_t c; doc_init(&c, "cap");
    static char big[DOC_CAPACITY];
    memset(big, 'x', sizeof(big));
    CHECK(doc_insert(&c, 0, big, DOC_CAPACITY), "fill to exact capacity");
    CHECK(doc_length(&c) == DOC_CAPACITY, "length == capacity");
    CHECK(!doc_insert(&c, 0, "overflow", 8), "insert past capacity REJECTED");
    CHECK(doc_length(&c) == DOC_CAPACITY, "length unchanged after rejection");

    /* search */
    doc_t f; doc_init(&f, "find");
    doc_insert(&f, 0, "the dragon guards the gate", 26);
    CHECK(doc_find(&f, "dragon", 0) == 4, "find 'dragon' at 4");
    CHECK(doc_find(&f, "the", 1) == 18, "find 'the' after index 1");
    CHECK(doc_find(&f, "wyvern", 0) == -1, "absent needle returns -1");

    /* lines */
    doc_t L; doc_init(&L, "lines");
    doc_insert(&L, 0, "one\ntwo\nthree", 13);
    CHECK(doc_line_count(&L) == 3, "3 lines");
    CHECK(doc_line_start(&L, 0) == 0, "line 0 starts at 0");
    CHECK(doc_line_start(&L, 1) == 4, "line 1 starts at 4");
    CHECK(doc_line_start(&L, 2) == 8, "line 2 starts at 8");

    /* bounded history: overflowing the undo depth must not corrupt */
    doc_t h; doc_init(&h, "hist");
    for (int i = 0; i < DOC_UNDO_DEPTH + 20; i++) {
        doc_insert(&h, doc_length(&h), "z", 1);
        doc_break_coalesce(&h);
    }
    CHECK(h.undo_count == DOC_UNDO_DEPTH, "undo history stays bounded");
    CHECK(doc_length(&h) == DOC_UNDO_DEPTH + 20, "all edits still applied");
    int u = 0; while (doc_undo(&h)) u++;
    CHECK(u == DOC_UNDO_DEPTH, "can undo exactly the retained depth");

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
