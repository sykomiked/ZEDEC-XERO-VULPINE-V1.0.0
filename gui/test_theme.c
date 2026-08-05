#include <stdio.h>
#include <string.h>
#include "theme.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

int main(void) {
    printf("=== theme_init fills all slots distinctly enough ===\n");
    theme_t dark, light, hc;
    theme_init(&dark, THEME_DARK);
    theme_init(&light, THEME_LIGHT);
    theme_init(&hc, THEME_HIGH_CONTRAST);

    CHECK(dark.id == THEME_DARK, "dark.id set correctly");
    CHECK(dark.colors[THEME_SLOT_BACKGROUND] != dark.colors[THEME_SLOT_TEXT],
          "dark theme: background and text are different colors (readable)");
    CHECK(light.colors[THEME_SLOT_BACKGROUND] != light.colors[THEME_SLOT_TEXT],
          "light theme: background and text are different colors (readable)");
    CHECK(hc.colors[THEME_SLOT_BACKGROUND] == RGB(0, 0, 0), "high-contrast background is pure black");
    CHECK(hc.colors[THEME_SLOT_TEXT] == RGB(255, 255, 255), "high-contrast text is pure white (max contrast)");

    printf("\n=== theme_color bounds safety ===\n");
    CHECK(theme_color(&dark, THEME_SLOT_ERROR) != 0 || dark.colors[THEME_SLOT_ERROR] == 0, "in-range slot returns the real stored color");
    CHECK(theme_color(&dark, (theme_slot_t)999) == RGB(255, 0, 255), "out-of-range slot returns magenta sentinel, not OOB read");

    printf("\n=== theme_get / theme_set_active ===\n");
    CHECK(theme_get(THEME_SLOT_TEXT) == RGB(255, 0, 255), "before any theme is set active, theme_get returns magenta sentinel");
    theme_set_active(&dark);
    CHECK(theme_get(THEME_SLOT_TEXT) == dark.colors[THEME_SLOT_TEXT], "after set_active(dark), theme_get matches dark's own color");
    theme_set_active(&light);
    CHECK(theme_get(THEME_SLOT_TEXT) == light.colors[THEME_SLOT_TEXT], "switching active theme to light updates theme_get immediately");

    printf("\n=== Naming ===\n");
    CHECK(strcmp(theme_slot_name(THEME_SLOT_BACKGROUND), "Background") == 0, "slot name Background correct");
    CHECK(strcmp(theme_name(THEME_HIGH_CONTRAST), "High Contrast") == 0, "theme name 'High Contrast' correct");
    CHECK(strcmp(theme_slot_name((theme_slot_t)999), "Unknown") == 0, "out-of-range slot name is Unknown, not garbage");

    if (failures == 0) printf("\n=== ALL THEME TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
