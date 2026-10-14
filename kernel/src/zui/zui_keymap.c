/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_keymap.c — layouts, the layout text loader, the built-in set and the
 * key-to-text translator with dead keys (zui_keymap.h). */
#include "zui_keymap.h"

/* ---- small helpers ----------------------------------------------------------- */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static bool tok_is(const char *t, uint32_t n, const char *lit)
{
    uint32_t i = 0;
    for (; i < n; i++)
        if (lit[i] != t[i]) return false;
    return lit[i] == 0;
}

static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void copy_name(char *dst, uint32_t cap, const char *s, uint32_t n)
{
    uint32_t i = 0;
    for (; i < n && i + 1 < cap; i++) dst[i] = s[i];
    dst[i] = 0;
}

static bool valid_cp(uint32_t cp)
{
    return cp != 0 && cp < 0x110000u && (cp < 0xD800u || cp > 0xDFFFu);
}

/* ---- UTF-8 and case ------------------------------------------------------------ */

uint32_t zui_km_utf8_put(char *d, uint32_t cp)
{
    if (!valid_cp(cp)) return 0;
    if (cp < 0x80u) {
        d[0] = (char) cp;
        return 1;
    }
    if (cp < 0x800u) {
        d[0] = (char) (0xC0u | (cp >> 6));
        d[1] = (char) (0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        d[0] = (char) (0xE0u | (cp >> 12));
        d[1] = (char) (0x80u | ((cp >> 6) & 0x3Fu));
        d[2] = (char) (0x80u | (cp & 0x3Fu));
        return 3;
    }
    d[0] = (char) (0xF0u | (cp >> 18));
    d[1] = (char) (0x80u | ((cp >> 12) & 0x3Fu));
    d[2] = (char) (0x80u | ((cp >> 6) & 0x3Fu));
    d[3] = (char) (0x80u | (cp & 0x3Fu));
    return 4;
}

uint32_t zui_km_utf8_get(const char *s, uint32_t n, uint32_t *cp)
{
    if (n == 0) {
        *cp = 0;
        return 0;
    }
    uint8_t b = (uint8_t) s[0];
    uint32_t len, v;
    if (b < 0x80u) {
        *cp = b;
        return 1;
    } else if ((b & 0xE0u) == 0xC0u) {
        len = 2;
        v = b & 0x1Fu;
    } else if ((b & 0xF0u) == 0xE0u) {
        len = 3;
        v = b & 0x0Fu;
    } else if ((b & 0xF8u) == 0xF0u) {
        len = 4;
        v = b & 0x07u;
    } else {
        *cp = 0xFFFDu;
        return 1;
    }
    if (len > n) {
        *cp = 0xFFFDu;
        return 1;
    }
    for (uint32_t i = 1; i < len; i++) {
        uint8_t c = (uint8_t) s[i];
        if ((c & 0xC0u) != 0x80u) {
            *cp = 0xFFFDu;
            return 1;
        }
        v = (v << 6) | (c & 0x3Fu);
    }
    /* overlong or out of range */
    if ((len == 2 && v < 0x80u) || (len == 3 && v < 0x800u) || (len == 4 && v < 0x10000u) ||
        !valid_cp(v)) {
        *cp = 0xFFFDu;
        return 1;
    }
    *cp = v;
    return len;
}

uint32_t zui_km_upper(uint32_t c)
{
    if (c >= 'a' && c <= 'z') return c - 32u;
    if (c >= 0xE0u && c <= 0xFEu && c != 0xF7u) return c - 32u;
    if (c == 0xFFu) return 0x178u;
    if (c >= 0x100u && c <= 0x137u && (c & 1u)) return c - 1u;
    if (c >= 0x139u && c <= 0x148u && !(c & 1u)) return c - 1u;
    if (c >= 0x14Au && c <= 0x177u && (c & 1u)) return c - 1u;
    if (c == 0x17Au || c == 0x17Cu || c == 0x17Eu) return c - 1u;
    if (c >= 0x3B1u && c <= 0x3C9u && c != 0x3C2u) return c - 32u;
    if (c == 0x3ACu) return 0x386u;
    if (c >= 0x3ADu && c <= 0x3AFu) return c - 37u;
    if (c == 0x3CAu || c == 0x3CBu) return c - 32u;
    if (c == 0x3CCu) return 0x38Cu;
    if (c == 0x3CDu || c == 0x3CEu) return c - 63u;
    if (c >= 0x430u && c <= 0x44Fu) return c - 32u;
    if (c >= 0x450u && c <= 0x45Fu) return c - 80u;
    return c;
}

uint32_t zui_km_lower(uint32_t c)
{
    if (c >= 'A' && c <= 'Z') return c + 32u;
    if (c >= 0xC0u && c <= 0xDEu && c != 0xD7u) return c + 32u;
    if (c == 0x178u) return 0xFFu;
    if (c >= 0x100u && c <= 0x137u && !(c & 1u)) return c + 1u;
    if (c >= 0x139u && c <= 0x148u && (c & 1u)) return c + 1u;
    if (c >= 0x14Au && c <= 0x177u && !(c & 1u)) return c + 1u;
    if (c == 0x179u || c == 0x17Bu || c == 0x17Du) return c + 1u;
    if (c >= 0x391u && c <= 0x3A9u && c != 0x3A2u) return c + 32u;
    if (c == 0x386u) return 0x3ACu;
    if (c >= 0x388u && c <= 0x38Au) return c + 37u;
    if (c == 0x3AAu || c == 0x3ABu) return c + 32u;
    if (c == 0x38Cu) return 0x3CCu;
    if (c == 0x38Eu || c == 0x38Fu) return c + 63u;
    if (c >= 0x410u && c <= 0x42Fu) return c + 32u;
    if (c >= 0x400u && c <= 0x40Fu) return c + 80u;
    return c;
}

bool zui_km_is_letter(uint32_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    if (c >= 0xC0u && c <= 0x24Fu) return c != 0xD7u && c != 0xF7u;
    if (c >= 0x386u && c <= 0x3FFu) return c != 0x387u;
    if (c >= 0x400u && c <= 0x4FFu) return true;
    if (c >= 0x5D0u && c <= 0x5EAu) return true; /* Hebrew        */
    if (c >= 0x620u && c <= 0x64Au) return true; /* Arabic        */
    if (c >= 0x671u && c <= 0x6D3u) return true;
    if (c >= 0x3041u && c <= 0x30FFu) return true; /* kana          */
    if (c >= 0x4E00u && c <= 0x9FFFu) return true; /* CJK ideographs */
    if (c >= 0xAC00u && c <= 0xD7A3u) return true; /* Hangul        */
    return false;
}

/* ---- K4: standard dead-key tables ------------------------------------------------ */

typedef struct {
    const char *name;
    uint32_t accent;
    const char *pairs; /* UTF-8: base, result, base, result ... */
} std_dead_t;

static const std_dead_t STD_DEAD[] = {
    {"grave", 0x60u, "aàeèiìoòuùAÀEÈIÌOÒUÙ"},
    {"acute", 0xB4u, "aáeéiíoóuúyýAÁEÉIÍOÓUÚYÝcćCĆnńNŃsśSŚzźZŹlĺLĹrŕRŔ"},
    {"circumflex", 0x5Eu, "aâeêiîoôuûAÂEÊIÎOÔUÛcĉCĈgĝGĜhĥHĤjĵJĴsŝSŜwŵWŴyŷYŶ"},
    {"tilde", 0x7Eu, "aãoõnñAÃOÕNÑiĩIĨuũUŨ"},
    {"diaeresis", 0xA8u, "aäeëiïoöuüyÿAÄEËIÏOÖUÜYŸιϊυϋΙΪΥΫ"},
    {"ring", 0x2DAu, "aåAÅuůUŮ"},
    {"cedilla", 0xB8u, "cçCÇsşSŞtţTŢgģGĢkķKĶlļLĻnņNŅrŗRŖ"},
    {"caron", 0x2C7u, "cčCČsšSŠzžZŽeěEĚrřRŘnňNŇdďDĎtťTŤ"},
    {"macron", 0xAFu, "aāeēiīoōuūAĀEĒIĪOŌUŪ"},
    {"tonos", 0x384u, "αάεέηήιίοόυύωώΑΆΕΈΗΉΙΊΟΌΥΎΩΏ"},
};
#define STD_DEAD_N (sizeof STD_DEAD / sizeof STD_DEAD[0])

static const std_dead_t *std_dead(const char *name, uint32_t n)
{
    for (uint32_t i = 0; i < STD_DEAD_N; i++)
        if (tok_is(name, n, STD_DEAD[i].name)) return &STD_DEAD[i];
    return 0;
}

/* Append a standard table's pairs; false when they do not fit. */
static bool add_std_pairs(zui_km_dead_t *d, const std_dead_t *s)
{
    const char *p = s->pairs;
    uint32_t n = slen(p), at = 0;
    while (at < n) {
        uint32_t a, b;
        at += zui_km_utf8_get(p + at, n - at, &a);
        at += zui_km_utf8_get(p + at, n - at, &b);
        if (d->n >= ZUI_KM_COMBO_MAX) return false;
        d->from[d->n] = a;
        d->to[d->n] = b;
        d->n++;
    }
    return true;
}

/* ---- K5: the built-in layouts, as layout text ---------------------------------- */

static const char L_US[] = "keyboard us English (US)\n"
                           "base  E00 ` 1 2 3 4 5 6 7 8 9 0 - =\n"
                           "base  D01 q w e r t y u i o p [ ] \\\n"
                           "base  C01 a s d f g h j k l ; '\n"
                           "base  B01 z x c v b n m , . /\n"
                           "shift E00 ~ ! @ # $ % ^ & * ( ) _ +\n"
                           "shift D01 Q W E R T Y U I O P { } |\n"
                           "shift C01 A S D F G H J K L : \"\n"
                           "shift B01 Z X C V B N M < > ?\n";

static const char L_UK[] = "keyboard uk English (UK)\n"
                           "base  E00 ` 1 2 3 4 5 6 7 8 9 0 - =\n"
                           "base  D01 q w e r t y u i o p [ ]\n"
                           "base  C01 a s d f g h j k l ; ' #\n"
                           "base  B00 \\ z x c v b n m , . /\n"
                           "shift E00 ¬ ! \" £ $ % ^ & * ( ) _ +\n"
                           "shift D01 Q W E R T Y U I O P { }\n"
                           "shift C01 A S D F G H J K L : @ ~\n"
                           "shift B00 | Z X C V B N M < > ?\n"
                           "altgr E00 ¦ -- -- -- €\n"
                           "altgr D03 é -- -- -- ú í ó\n"
                           "altgr C01 á\n"
                           "altgr+shift D03 É -- -- -- Ú Í Ó\n"
                           "altgr+shift C01 Á\n";

static const char L_USINTL[] = "keyboard us-intl English (US, international, dead keys)\n"
                               "deadkey igrave ` =grave\n"
                               "deadkey iacute ' aá eé ií oó uú yý AÁ EÉ IÍ OÓ UÚ YÝ cç CÇ\n"
                               "deadkey icirc ^ =circumflex\n"
                               "deadkey itilde ~ =tilde\n"
                               "deadkey idiaer \" =diaeresis\n"
                               "base  E00 dead:igrave 1 2 3 4 5 6 7 8 9 0 - =\n"
                               "base  D01 q w e r t y u i o p [ ] \\\n"
                               "base  C01 a s d f g h j k l ; dead:iacute\n"
                               "base  B01 z x c v b n m , . /\n"
                               "shift E00 dead:itilde ! @ # $ % dead:icirc & * ( ) _ +\n"
                               "shift D01 Q W E R T Y U I O P { } |\n"
                               "shift C01 A S D F G H J K L : dead:idiaer\n"
                               "shift B01 Z X C V B N M < > ?\n"
                               "altgr E01 ¡ ² ³ ¤ € ¼ ½ ¾ ‘ ’ ¥ ×\n"
                               "altgr D01 ä å é ® þ ü ú í ó ö « » ¬\n"
                               "altgr C01 á ß ð -- -- -- -- -- ø ¶ ´\n"
                               "altgr B01 æ -- © -- -- ñ µ ç -- ¿\n"
                               "altgr+shift E01 ¹ -- -- £ -- -- -- -- -- -- -- ÷\n"
                               "altgr+shift D01 Ä Å É -- Þ Ü Ú Í Ó Ö -- -- ¦\n"
                               "altgr+shift C01 Á § Ð -- -- -- -- -- Ø ° ¨\n"
                               "altgr+shift B01 Æ -- ¢ -- -- Ñ -- Ç -- --\n";

static const char L_DE[] = "keyboard de German (QWERTZ)\n"
                           "base  E00 dead:circumflex 1 2 3 4 5 6 7 8 9 0 ß dead:acute\n"
                           "base  D01 q w e r t z u i o p ü +\n"
                           "base  C01 a s d f g h j k l ö ä #\n"
                           "base  B00 < y x c v b n m , . -\n"
                           "shift E00 ° ! \" § $ % & / ( ) = ? dead:grave\n"
                           "shift D01 Q W E R T Z U I O P Ü *\n"
                           "shift C01 A S D F G H J K L Ö Ä '\n"
                           "shift B00 > Y X C V B N M ; : _\n"
                           "altgr E02 ² ³ -- -- -- { [ ] } \\\n"
                           "altgr D01 @ -- € -- -- -- -- -- -- -- -- ~\n"
                           "altgr B00 | -- -- -- -- -- -- µ\n";

static const char L_FR[] = "keyboard fr French (AZERTY)\n"
                           "base  E00 ² & é \" ' ( - è _ ç à ) =\n"
                           "base  D01 a z e r t y u i o p dead:circumflex $\n"
                           "base  C01 q s d f g h j k l m ù *\n"
                           "base  B00 < w x c v b n , ; : !\n"
                           "shift E00 -- 1 2 3 4 5 6 7 8 9 0 ° +\n"
                           "shift D01 A Z E R T Y U I O P dead:diaeresis £\n"
                           "shift C01 Q S D F G H J K L M % µ\n"
                           "shift B00 > W X C V B N ? . / §\n"
                           "altgr E02 dead:tilde # { [ | dead:grave \\ ^ @ ] }\n"
                           "altgr D03 €\n"
                           "altgr D12 ¤\n";

static const char L_ES[] = "keyboard es Spanish\n"
                           "base  E00 º 1 2 3 4 5 6 7 8 9 0 ' ¡\n"
                           "base  D01 q w e r t y u i o p dead:grave +\n"
                           "base  C01 a s d f g h j k l ñ dead:acute ç\n"
                           "base  B00 < z x c v b n m , . -\n"
                           "shift E00 ª ! \" · $ % & / ( ) = ? ¿\n"
                           "shift D01 Q W E R T Y U I O P dead:circumflex *\n"
                           "shift C01 A S D F G H J K L Ñ dead:diaeresis Ç\n"
                           "shift B00 > Z X C V B N M ; : _\n"
                           "altgr E00 \\ | @ # dead:tilde € ¬\n"
                           "altgr D03 €\n"
                           "altgr D11 [ ]\n"
                           "altgr C11 { }\n";

static const char L_IT[] = "keyboard it Italian\n"
                           "base  E00 \\ 1 2 3 4 5 6 7 8 9 0 ' ì\n"
                           "base  D01 q w e r t y u i o p è +\n"
                           "base  C01 a s d f g h j k l ò à ù\n"
                           "base  B00 < z x c v b n m , . -\n"
                           "shift E00 | ! \" £ $ % & / ( ) = ? ^\n"
                           "shift D01 Q W E R T Y U I O P é *\n"
                           "shift C01 A S D F G H J K L ç ° §\n"
                           "shift B00 > Z X C V B N M ; : _\n"
                           "altgr D03 €\n"
                           "altgr D11 [ ]\n"
                           "altgr C10 @ #\n"
                           "altgr+shift D11 { }\n";

static const char L_PTBR[] = "keyboard pt-br Portuguese (Brazil, ABNT2)\n"
                             "base  E00 ' 1 2 3 4 5 6 7 8 9 0 - =\n"
                             "base  D01 q w e r t y u i o p dead:acute [\n"
                             "base  C01 a s d f g h j k l ç dead:tilde ]\n"
                             "base  B00 \\ z x c v b n m , . ; /\n"
                             "shift E00 \" ! @ # $ % dead:diaeresis & * ( ) _ +\n"
                             "shift D01 Q W E R T Y U I O P dead:grave {\n"
                             "shift C01 A S D F G H J K L Ç dead:circumflex }\n"
                             "shift B00 | Z X C V B N M < > : ?\n"
                             "altgr E01 ¹ ² ³ £ ¢ ¬ -- -- -- -- -- §\n"
                             "altgr D01 / ? ° -- -- -- -- -- -- -- -- ª\n"
                             "altgr C12 º\n";

static const char L_SE[] = "keyboard se Swedish (Nordic)\n"
                           "base  E00 § 1 2 3 4 5 6 7 8 9 0 + dead:acute\n"
                           "base  D01 q w e r t y u i o p å dead:diaeresis\n"
                           "base  C01 a s d f g h j k l ö ä '\n"
                           "base  B00 < z x c v b n m , . -\n"
                           "shift E00 ½ ! \" # ¤ % & / ( ) = ? dead:grave\n"
                           "shift D01 Q W E R T Y U I O P Å dead:circumflex\n"
                           "shift C01 A S D F G H J K L Ö Ä *\n"
                           "shift B00 > Z X C V B N M ; : _\n"
                           "altgr E02 @ £ $ € -- { [ ] } \\\n"
                           "altgr D03 €\n"
                           "altgr D12 dead:tilde\n"
                           "altgr B00 | -- -- -- -- -- -- µ\n";

static const char L_DVORAK[] = "keyboard dvorak English (Dvorak)\n"
                               "base  E00 ` 1 2 3 4 5 6 7 8 9 0 [ ]\n"
                               "base  D01 ' , . p y f g c r l / = \\\n"
                               "base  C01 a o e u i d h t n s -\n"
                               "base  B01 ; q j k x b m w v z\n"
                               "shift E00 ~ ! @ # $ % ^ & * ( ) { }\n"
                               "shift D01 \" < > P Y F G C R L ? + |\n"
                               "shift C01 A O E U I D H T N S _\n"
                               "shift B01 : Q J K X B M W V Z\n";

static const char L_COLEMAK[] = "keyboard colemak English (Colemak)\n"
                                "base  E00 ` 1 2 3 4 5 6 7 8 9 0 - =\n"
                                "base  D01 q w f p g j l u y ; [ ] \\\n"
                                "base  C01 a r s t d h n e i o '\n"
                                "base  B01 z x c v b k m , . /\n"
                                "shift E00 ~ ! @ # $ % ^ & * ( ) _ +\n"
                                "shift D01 Q W F P G J L U Y : { } |\n"
                                "shift C01 A R S T D H N E I O \"\n"
                                "shift B01 Z X C V B K M < > ?\n";

static const char L_RU[] = "keyboard ru Russian (ЙЦУКЕН)\n"
                           "base  E00 ё 1 2 3 4 5 6 7 8 9 0 - =\n"
                           "base  D01 й ц у к е н г ш щ з х ъ \\\n"
                           "base  C01 ф ы в а п р о л д ж э\n"
                           "base  B01 я ч с м и т ь б ю .\n"
                           "shift E00 Ё ! \" № ; % : ? * ( ) _ +\n"
                           "shift D01 Й Ц У К Е Н Г Ш Щ З Х Ъ /\n"
                           "shift C01 Ф Ы В А П Р О Л Д Ж Э\n"
                           "shift B01 Я Ч С М И Т Ь Б Ю ,\n";

static const char L_GR[] = "keyboard gr Greek\n"
                           "base  E00 ` 1 2 3 4 5 6 7 8 9 0 - =\n"
                           "base  D01 ; ς ε ρ τ υ θ ι ο π [ ] \\\n"
                           "base  C01 α σ δ φ γ η ξ κ λ dead:tonos '\n"
                           "base  B01 ζ χ ψ ω β ν μ , . /\n"
                           "shift E00 ~ ! @ # $ % ^ & * ( ) _ +\n"
                           "shift D01 : ΅ Ε Ρ Τ Υ Θ Ι Ο Π { } |\n"
                           "shift C01 Α Σ Δ Φ Γ Η Ξ Κ Λ dead:diaeresis \"\n"
                           "shift B01 Ζ Χ Ψ Ω Β Ν Μ < > ?\n"
                           "altgr D03 €\n";

static const char L_HE[] = "keyboard he Hebrew\n"
                           "base  E00 ; 1 2 3 4 5 6 7 8 9 0 - =\n"
                           "base  D01 / ' ק ר א ט ו ן ם פ ] [ \\\n"
                           "base  C01 ש ד ג כ ע י ח ל ך ף ,\n"
                           "base  B01 ז ס ב ה נ מ צ ת ץ .\n"
                           "shift E00 ~ ! @ # $ % ^ & * ) ( _ +\n"
                           "shift D01 Q W E R T Y U I O P } { |\n"
                           "shift C01 A S D F G H J K L : \"\n"
                           "shift B01 Z X C V B N M > < ?\n"
                           "altgr E04 ₪\n"
                           "altgr D03 €\n";

static const char L_AR[] = "keyboard ar Arabic\n"
                           "base  E00 ذ 1 2 3 4 5 6 7 8 9 0 - =\n"
                           "base  D01 ض ص ث ق ف غ ع ه خ ح ج د \\\n"
                           "base  C01 ش س ي ب ل ا ت ن م ك ط\n"
                           "base  B01 ئ ء ؤ ر لا ى ة و ز ظ\n"
                           "shift E00 \\u{0651} ! @ # $ % ^ & * ) ( _ +\n"
                           "shift D01 \\u{064E} \\u{064B} \\u{064F} \\u{064C} لإ إ ‘ ÷ × ؛ < > |\n"
                           "shift C01 \\u{0650} \\u{064D} ] [ لأ أ \\u0640 ، / : \"\n"
                           "shift B01 ~ \\u{0652} } { لآ آ ’ , . ؟\n";

static const struct {
    const char *id;
    const char *text;
} BUILTIN[] = {
    {"us", L_US},           {"uk", L_UK}, {"us-intl", L_USINTL}, {"de", L_DE}, {"fr", L_FR},
    {"es", L_ES},           {"it", L_IT}, {"pt-br", L_PTBR},     {"se", L_SE}, {"dvorak", L_DVORAK},
    {"colemak", L_COLEMAK}, {"ru", L_RU}, {"gr", L_GR},          {"he", L_HE}, {"ar", L_AR},
};
#define BUILTIN_N (sizeof BUILTIN / sizeof BUILTIN[0])

uint32_t zui_keymap_builtin_count(void)
{
    return (uint32_t) BUILTIN_N;
}

const char *zui_keymap_builtin_id(uint32_t i)
{
    return i < BUILTIN_N ? BUILTIN[i].id : 0;
}

const char *zui_keymap_builtin_text(uint32_t i)
{
    return i < BUILTIN_N ? BUILTIN[i].text : 0;
}

bool zui_keymap_load(zui_keymap_t *km, const char *id)
{
    for (uint32_t i = 0; i < BUILTIN_N; i++) {
        if (tok_is(id, slen(id), BUILTIN[i].id) && slen(id) == slen(BUILTIN[i].id))
            return zui_keymap_parse(km, BUILTIN[i].text, slen(BUILTIN[i].text), 0);
    }
    zero(km, (uint32_t) sizeof *km);
    return false;
}

/* ---- K5: the parser -------------------------------------------------------------- */

#define TOK_MAX 40

typedef struct {
    const char *p;
    uint32_t n;
} tok_t;

static int32_t hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A token's code points, \u escapes decoded. -1 on a malformed token. */
static int32_t tok_cps(const char *t, uint32_t n, uint32_t *cps, uint32_t max)
{
    uint32_t at = 0, k = 0;
    while (at < n) {
        uint32_t cp;
        if (t[at] == '\\' && at + 1 < n && t[at + 1] == 'u' && at + 2 < n &&
            (t[at + 2] == '{' || hexval(t[at + 2]) >= 0)) {
            at += 2;
            cp = 0;
            if (t[at] == '{') {
                at++;
                uint32_t digits = 0;
                while (at < n && t[at] != '}') {
                    int32_t h = hexval(t[at]);
                    if (h < 0 || digits >= 6) return -1;
                    cp = (cp << 4) | (uint32_t) h;
                    digits++;
                    at++;
                }
                if (at >= n || digits == 0) return -1;
                at++; /* '}' */
            } else {
                for (uint32_t i = 0; i < 4; i++, at++) {
                    if (at >= n) return -1;
                    int32_t h = hexval(t[at]);
                    if (h < 0) return -1;
                    cp = (cp << 4) | (uint32_t) h;
                }
            }
            if (!valid_cp(cp)) return -1;
        } else {
            uint32_t used = zui_km_utf8_get(t + at, n - at, &cp);
            if (cp == 0xFFFDu && used == 1 && (uint8_t) t[at] >= 0x80u) return -1;
            at += used;
        }
        if (k >= max) return -1;
        cps[k++] = cp;
    }
    return (int32_t) k;
}

static int32_t find_dead(const zui_keymap_t *km, const char *name, uint32_t n)
{
    for (uint32_t i = 0; i < km->ndead; i++)
        if (tok_is(name, n, km->dead[i].name)) return (int32_t) i;
    return -1;
}

/* A slot for dead key `name`: the existing one, else a fresh empty one. */
static int32_t dead_slot(zui_keymap_t *km, const char *name, uint32_t n)
{
    int32_t d = find_dead(km, name, n);
    if (d >= 0) return d;
    if (km->ndead >= ZUI_KM_DEAD_MAX || n == 0 || n >= sizeof km->dead[0].name) return -1;
    zui_km_dead_t *s = &km->dead[km->ndead];
    zero(s, (uint32_t) sizeof *s);
    copy_name(s->name, (uint32_t) sizeof s->name, name, n);
    return (int32_t) km->ndead++;
}

/* A key value from a token; false on error. */
static bool tok_value(zui_keymap_t *km, const tok_t *t, uint32_t *out)
{
    if (tok_is(t->p, t->n, "--")) {
        *out = 0;
        return true;
    }
    if (t->n > 5 && tok_is(t->p, 5, "dead:")) {
        const char *nm = t->p + 5;
        uint32_t nn = t->n - 5;
        int32_t d = find_dead(km, nm, nn);
        if (d < 0) {
            const std_dead_t *s = std_dead(nm, nn);
            if (!s) return false;
            d = dead_slot(km, nm, nn);
            if (d < 0) return false;
            km->dead[d].accent = s->accent;
            if (!add_std_pairs(&km->dead[d], s)) return false;
        }
        *out = ZUI_KM_DEAD | (uint32_t) d;
        return true;
    }
    uint32_t cps[ZUI_KM_STR_BYTES];
    int32_t k = tok_cps(t->p, t->n, cps, ZUI_KM_STR_BYTES);
    if (k <= 0) return false;
    if (k == 1) {
        *out = cps[0];
        return true;
    }
    char buf[ZUI_KM_STR_BYTES + 4];
    uint32_t len = 0;
    for (int32_t i = 0; i < k; i++) {
        if (len + 4 > ZUI_KM_STR_BYTES + 3) return false;
        len += zui_km_utf8_put(buf + len, cps[i]);
    }
    if (len >= ZUI_KM_STR_BYTES) return false;
    buf[len] = 0;
    for (uint32_t s = 0; s < km->nstr; s++) {
        if (tok_is(buf, len, km->str[s]) && slen(km->str[s]) == len) {
            *out = ZUI_KM_STR | s;
            return true;
        }
    }
    if (km->nstr >= ZUI_KM_STR_MAX) return false;
    for (uint32_t i = 0; i <= len; i++) km->str[km->nstr][i] = buf[i];
    *out = ZUI_KM_STR | km->nstr++;
    return true;
}

/* "E00" etc. -> position; -1 if malformed. */
static int32_t parse_pos(const tok_t *t)
{
    if (t->n != 3) return -1;
    int32_t a = t->p[1] - '0', b = t->p[2] - '0';
    if (a < 0 || a > 9 || b < 0 || b > 9) return -1;
    int32_t col = a * 10 + b;
    switch (t->p[0]) {
    case 'E':
        return col < 16 ? ZUI_KP(ZUI_KP_ROW_E, col) : -1;
    case 'D':
        return col < 16 ? ZUI_KP(ZUI_KP_ROW_D, col) : -1;
    case 'C':
        return col < 16 ? ZUI_KP(ZUI_KP_ROW_C, col) : -1;
    case 'B':
        return col < 16 ? ZUI_KP(ZUI_KP_ROW_B, col) : -1;
    case 'A':
        return col == 3 ? ZUI_KP_SPACE : -1;
    default:
        return -1;
    }
}

static int32_t parse_level(const tok_t *t)
{
    if (tok_is(t->p, t->n, "base")) return ZUI_KM_BASE;
    if (tok_is(t->p, t->n, "shift")) return ZUI_KM_SHIFT;
    if (tok_is(t->p, t->n, "altgr") || tok_is(t->p, t->n, "altR")) return ZUI_KM_ALTGR;
    if (tok_is(t->p, t->n, "altgr+shift") || tok_is(t->p, t->n, "altR+shift"))
        return ZUI_KM_ALTGR_SHIFT;
    return -1;
}

static bool plain(uint32_t v)
{
    return v != 0 && !ZUI_KM_IS_DEAD(v) && !ZUI_KM_IS_STR(v);
}

/* K3: which keys Caps Lock flips. */
static void finish(zui_keymap_t *km)
{
    km->has_altgr = false;
    for (uint32_t p = 0; p < ZUI_KP_COUNT; p++) {
        const uint32_t *k = km->key[p];
        if (k[ZUI_KM_ALTGR] || k[ZUI_KM_ALTGR_SHIFT]) km->has_altgr = true;
        uint8_t f = 0;
        for (uint32_t pair = 0; pair < 2; pair++) {
            uint32_t lo = k[pair * 2], hi = k[pair * 2 + 1];
            bool flip = false;
            if (km->caps_mode == ZUI_CAPS_SHIFT)
                flip = hi != 0;
            else if (km->caps_mode == ZUI_CAPS_LETTERS)
                flip = plain(lo) && plain(hi) && lo != hi && zui_km_upper(lo) == hi;
            if (flip) f |= (uint8_t) (1u << pair);
        }
        km->caps[p] = f;
    }
    /* the space bar types a space unless the layout says otherwise */
    if (!km->key[ZUI_KP_SPACE][ZUI_KM_BASE]) km->key[ZUI_KP_SPACE][ZUI_KM_BASE] = ' ';
    if (!km->key[ZUI_KP_SPACE][ZUI_KM_SHIFT]) km->key[ZUI_KP_SPACE][ZUI_KM_SHIFT] = ' ';
}

static bool parse_line(zui_keymap_t *km, const tok_t *tk, uint32_t nt)
{
    if (tok_is(tk[0].p, tk[0].n, "keyboard")) {
        if (nt < 2) return false;
        copy_name(km->id, (uint32_t) sizeof km->id, tk[1].p, tk[1].n);
        uint32_t at = 0;
        for (uint32_t i = 2; i < nt; i++) {
            for (uint32_t j = 0; j < tk[i].n && at + 1 < sizeof km->name; j++)
                km->name[at++] = tk[i].p[j];
            if (i + 1 < nt && at + 1 < sizeof km->name) km->name[at++] = ' ';
        }
        km->name[at] = 0;
        return true;
    }
    if (tok_is(tk[0].p, tk[0].n, "caps")) {
        if (nt != 2) return false;
        if (tok_is(tk[1].p, tk[1].n, "letters"))
            km->caps_mode = ZUI_CAPS_LETTERS;
        else if (tok_is(tk[1].p, tk[1].n, "shift"))
            km->caps_mode = ZUI_CAPS_SHIFT;
        else if (tok_is(tk[1].p, tk[1].n, "none"))
            km->caps_mode = ZUI_CAPS_NONE;
        else
            return false;
        return true;
    }
    if (tok_is(tk[0].p, tk[0].n, "deadkey")) {
        if (nt < 3) return false;
        int32_t d = dead_slot(km, tk[1].p, tk[1].n);
        if (d < 0) return false;
        zui_km_dead_t *s = &km->dead[d];
        s->n = 0;
        uint32_t cps[4];
        if (tok_cps(tk[2].p, tk[2].n, cps, 4) != 1) return false;
        s->accent = cps[0];
        for (uint32_t i = 3; i < nt; i++) {
            if (tk[i].n > 1 && tk[i].p[0] == '=') {
                const std_dead_t *sd = std_dead(tk[i].p + 1, tk[i].n - 1);
                if (!sd || !add_std_pairs(s, sd)) return false;
                continue;
            }
            if (tok_cps(tk[i].p, tk[i].n, cps, 4) != 2 || s->n >= ZUI_KM_COMBO_MAX) return false;
            s->from[s->n] = cps[0];
            s->to[s->n] = cps[1];
            s->n++;
        }
        return true;
    }
    int32_t lv = parse_level(&tk[0]);
    if (lv < 0 || nt < 3) return false;
    int32_t pos = parse_pos(&tk[1]);
    if (pos < 0) return false;
    for (uint32_t i = 2; i < nt; i++) {
        int32_t p = pos + (int32_t) (i - 2);
        /* stay in the starting row */
        if (pos == ZUI_KP_SPACE ? p != ZUI_KP_SPACE : (p >> 4) != (pos >> 4)) return false;
        uint32_t v;
        if (!tok_value(km, &tk[i], &v)) return false;
        km->key[p][lv] = v;
    }
    return true;
}

bool zui_keymap_parse(zui_keymap_t *km, const char *text, uint32_t len, uint32_t *err_line)
{
    zero(km, (uint32_t) sizeof *km);
    uint32_t at = 0, line = 0;
    if (err_line) *err_line = 0;
    while (at < len && text[at]) {
        line++;
        uint32_t end = at;
        while (end < len && text[end] && text[end] != '\n') end++;
        uint32_t e = end;
        if (e > at && text[e - 1] == '\r') e--;
        tok_t tk[TOK_MAX];
        uint32_t nt = 0;
        bool too_many = false;
        for (uint32_t i = at; i < e;) {
            while (i < e && (text[i] == ' ' || text[i] == '\t')) i++;
            if (i >= e) break;
            uint32_t s = i;
            while (i < e && text[i] != ' ' && text[i] != '\t') i++;
            if (nt >= TOK_MAX) {
                too_many = true;
                break;
            }
            tk[nt].p = text + s;
            tk[nt].n = i - s;
            nt++;
        }
        bool ok = true;
        if (too_many)
            ok = false;
        else if (nt > 0 && tk[0].p[0] != '#')
            ok = parse_line(km, tk, nt);
        if (!ok) {
            zero(km, (uint32_t) sizeof *km);
            if (err_line) *err_line = line;
            return false;
        }
        at = end < len && text[end] == '\n' ? end + 1 : end;
        if (end >= len || !text[end]) break;
    }
    finish(km);
    return true;
}

/* ---- lookups ---------------------------------------------------------------------- */

static const int8_t EVDEV_POS[90] = {
    -1, -1, 1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, -1, -1, /* 0..15  */
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, -1, -1, 33, 34, /* 16..31 */
    35, 36, 37, 38, 39, 40, 41, 42, 43, 0,  -1, 29, 49, 50, 51, 52, /* 32..47 */
    53, 54, 55, 56, 57, 58, -1, -1, -1, 64, -1, -1, -1, -1, -1, -1, /* 48..63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 64..79 */
    -1, -1, -1, -1, -1, -1, 48, -1, -1, 59};                        /* 80..89 */

int32_t zui_kpos_from_evdev(uint16_t code)
{
    if (code >= sizeof EVDEV_POS) return -1;
    return EVDEV_POS[code];
}

uint16_t zui_evdev_from_hid(uint16_t u)
{
    static const uint8_t letters[26] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                        49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
    static const uint8_t from_28[] = {
        28, 1,   14,  15,  57,  12,  13,  26,  27,  43,  43,  39, 40, 41, 51, 52, /* 0x28..0x37 */
        53, 58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68, 87, 88, 0,  0,  /* 0x38..0x47 */
        0,  110, 102, 104, 111, 107, 109, 106, 105, 108, 103, 0,  98, 55, 74, 78, /* 0x48.. */
        96, 79,  80,  81,  75,  76,  77,  71,  72,  73,  82,  83, 86, 127};       /* 0x58..0x65 */
    static const uint8_t mods[8] = {29, 42, 56, 125, 97, 54, 100, 126};
    if (u >= 0x04 && u <= 0x1D) return letters[u - 0x04];
    if (u >= 0x1E && u <= 0x26) return (uint16_t) (u - 0x1E + 2);
    if (u == 0x27) return 11;
    if (u >= 0x28 && u < 0x28 + sizeof from_28) return from_28[u - 0x28];
    if (u == 0x87) return 89;
    if (u >= 0xE0 && u <= 0xE7) return mods[u - 0xE0];
    return 0;
}

static bool row_empty(const zui_keymap_t *km, int32_t p)
{
    for (uint32_t l = 0; l < ZUI_KM_LEVELS; l++)
        if (km->key[p][l]) return false;
    return true;
}

static int32_t effective_pos(const zui_keymap_t *km, int32_t pos)
{
    if (pos == ZUI_KP_D13 && row_empty(km, pos)) return ZUI_KP_C12;
    if (pos == ZUI_KP_C12 && row_empty(km, pos)) return ZUI_KP_D13;
    return pos;
}

uint32_t zui_keymap_get(const zui_keymap_t *km, int32_t pos, bool shift, bool altgr, bool caps)
{
    if (!km || pos < 0 || pos >= ZUI_KP_COUNT) return 0;
    pos = effective_pos(km, pos);
    uint32_t pair = altgr ? 1u : 0u;
    bool flip = caps && (km->caps[pos] & (1u << pair));
    uint32_t lv = pair * 2u + ((shift != flip) ? 1u : 0u);
    return km->key[pos][lv];
}

uint32_t zui_keymap_utf8(const zui_keymap_t *km, uint32_t v, char *dst, uint32_t cap)
{
    if (cap == 0) return 0;
    char tmp[ZUI_KM_STR_BYTES];
    uint32_t n = 0;
    if (ZUI_KM_IS_DEAD(v)) {
        uint32_t d = v & 0xFFu;
        if (d < km->ndead) n = zui_km_utf8_put(tmp, km->dead[d].accent);
    } else if (ZUI_KM_IS_STR(v)) {
        uint32_t s = v & 0xFFu;
        if (s < km->nstr)
            for (; km->str[s][n]; n++) tmp[n] = km->str[s][n];
    } else if (v) {
        n = zui_km_utf8_put(tmp, v);
    }
    if (n + 1 > cap) n = 0;
    for (uint32_t i = 0; i < n; i++) dst[i] = tmp[i];
    dst[n] = 0;
    return n;
}

/* The US letter at each position, for K6. */
static const char US_KEY[64] = {
    '`',  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',  0,    0, 0,  /* E */
    0,    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',  '\\', 0, 0,  /* D */
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\\', 0,    0, 0,  /* C */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', '/',  0,    0,    0, 0}; /* B */

uint16_t zui_keymap_shortcut(const zui_keymap_t *km, int32_t pos)
{
    if (pos == ZUI_KP_SPACE) return ZUI_KEY_SPACE;
    if (pos < 0 || pos >= 64) return ZUI_KEY_NONE;
    if (km) {
        int32_t p = effective_pos(km, pos);
        uint32_t b = km->key[p][ZUI_KM_BASE], s = km->key[p][ZUI_KM_SHIFT];
        if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z')) return (uint16_t) zui_km_lower(b);
        if (s >= '0' && s <= '9') return (uint16_t) s;
        if (b > 0x20u && b < 0x7Fu) return (uint16_t) b;
    }
    return (uint16_t) (uint8_t) US_KEY[pos];
}

uint32_t zui_keymap_combine(const zui_keymap_t *km, uint32_t d, uint32_t cp)
{
    if (!km || d >= km->ndead) return 0;
    const zui_km_dead_t *t = &km->dead[d];
    for (uint32_t i = t->n; i > 0; i--) /* a later pair overrides an earlier one */
        if (t->from[i - 1] == cp) return t->to[i - 1];
    return 0;
}

/* ---- K4, K6: the translator -------------------------------------------------------- */

enum {
    H_LSHIFT = 1u << 0,
    H_RSHIFT = 1u << 1,
    H_LCTRL = 1u << 2,
    H_RCTRL = 1u << 3,
    H_LALT = 1u << 4,
    H_RALT = 1u << 5,
    H_LMETA = 1u << 6,
    H_RMETA = 1u << 7,
    H_ALTGR = 1u << 8
};

void zui_kbd_init(zui_kbd_t *k, const zui_keymap_t *km)
{
    k->km = km;
    k->held = 0;
    k->caps = false;
    k->dead = -1;
    k->cmd_is_meta = false;
    k->dead_preedit = true;
    k->compose_code = 0;
}

void zui_kbd_set_keymap(zui_kbd_t *k, const zui_keymap_t *km)
{
    k->km = km;
    k->dead = -1;
    k->held &= (uint16_t) ~H_ALTGR;
}

uint8_t zui_kbd_mods(const zui_kbd_t *k)
{
    uint8_t m = 0;
    if (k->held & (H_LSHIFT | H_RSHIFT)) m |= ZUI_MOD_SHIFT;
    if (k->held & (H_LCTRL | H_RCTRL)) m |= ZUI_MOD_CTRL;
    if (k->held & (H_LALT | H_RALT)) m |= ZUI_MOD_ALT;
    if (k->held & (H_LMETA | H_RMETA)) m |= ZUI_MOD_META;
    if (k->cmd_is_meta ? (m & ZUI_MOD_META) : (m & ZUI_MOD_CTRL)) m |= ZUI_MOD_CMD;
    return m;
}

static uint16_t modifier_bit(const zui_kbd_t *k, uint16_t code)
{
    switch (code) {
    case 42:
        return H_LSHIFT;
    case 54:
        return H_RSHIFT;
    case 29:
        return H_LCTRL;
    case 97:
        return H_RCTRL;
    case 56:
        return H_LALT;
    case 100:
        return (k->km && k->km->has_altgr) ? H_ALTGR : H_RALT;
    case 125:
        return H_LMETA;
    case 126:
        return H_RMETA;
    default:
        return 0;
    }
}

/* Keys that are not text: evdev code -> ZUI_KEY_*. */
static uint16_t special_key(uint16_t code)
{
    switch (code) {
    case 1:
        return ZUI_KEY_ESCAPE;
    case 14:
        return ZUI_KEY_BACKSPACE;
    case 15:
        return ZUI_KEY_TAB;
    case 28:
    case 96:
        return ZUI_KEY_ENTER;
    case 102:
        return ZUI_KEY_HOME;
    case 103:
        return ZUI_KEY_UP;
    case 104:
        return ZUI_KEY_PAGE_UP;
    case 105:
        return ZUI_KEY_LEFT;
    case 106:
        return ZUI_KEY_RIGHT;
    case 107:
        return ZUI_KEY_END;
    case 108:
        return ZUI_KEY_DOWN;
    case 109:
        return ZUI_KEY_PAGE_DOWN;
    case 110:
        return ZUI_KEY_INSERT;
    case 111:
        return ZUI_KEY_DELETE;
    case 87:
        return ZUI_KEY_F1 + 10;
    case 88:
        return ZUI_KEY_F12;
    case 127:
        return ZUI_KEY_MENU;
    case 158:
        return ZUI_KEY_BACK;
    default:
        if (code >= 59 && code <= 68) return (uint16_t) (ZUI_KEY_F1 + (code - 59));
        return ZUI_KEY_NONE;
    }
}

/* Keypad keys type their character whatever the layout (Num Lock on). */
static uint32_t keypad_char(uint16_t code)
{
    switch (code) {
    case 71:
        return '7';
    case 72:
        return '8';
    case 73:
        return '9';
    case 74:
        return '-';
    case 75:
        return '4';
    case 76:
        return '5';
    case 77:
        return '6';
    case 78:
        return '+';
    case 79:
        return '1';
    case 80:
        return '2';
    case 81:
        return '3';
    case 82:
        return '0';
    case 83:
        return '.';
    case 98:
        return '/';
    case 55:
        return '*';
    default:
        return 0;
    }
}

typedef struct {
    zui_event_t *out;
    uint32_t cap, n;
    uint32_t ms;
    uint8_t mods;
} sink_t;

static zui_event_t *emit(sink_t *s, uint8_t type)
{
    if (s->n >= s->cap) return 0;
    zui_event_t *e = &s->out[s->n++];
    zero(e, (uint32_t) sizeof *e);
    e->type = type;
    e->ms = s->ms;
    e->mods = s->mods;
    return e;
}

static void emit_key(sink_t *s, bool down, uint16_t key, bool repeat)
{
    zui_event_t *e = emit(s, down ? ZUI_EV_KEY_DOWN : ZUI_EV_KEY_UP);
    if (!e) return;
    e->key = key;
    e->repeat = repeat ? 1 : 0;
}

static void emit_text(sink_t *s, uint8_t type, const char *t)
{
    zui_event_t *e = emit(s, type);
    if (!e) return;
    uint32_t n = slen(t);
    n = zui_utf8_fit(t, n, ZUI_TEXT_EV_MAX);
    for (uint32_t i = 0; i < n; i++) e->text[i] = t[i];
    e->text[n] = 0;
}

static void dead_show(zui_kbd_t *k, sink_t *s)
{
    if (!k->dead_preedit) return;
    char buf[8];
    zui_keymap_utf8(k->km, ZUI_KM_DEAD | (uint32_t) k->dead, buf, sizeof buf);
    emit_text(s, ZUI_EV_PREEDIT, buf);
}

static void dead_clear(zui_kbd_t *k, sink_t *s)
{
    if (k->dead >= 0 && k->dead_preedit) emit_text(s, ZUI_EV_PREEDIT, "");
    k->dead = -1;
}

/* K4: type value v, resolving a waiting dead key. */
static void type_value(zui_kbd_t *k, sink_t *s, uint32_t v, bool is_space, bool repeat)
{
    const zui_keymap_t *km = k->km;
    char buf[ZUI_TEXT_EV_MAX + 1];
    uint32_t n = 0;
    if (!v) return; /* nothing on this level: a waiting dead key keeps waiting */
    if (ZUI_KM_IS_DEAD(v)) {
        if (repeat) return;
        uint32_t nd = v & 0xFFu;
        if (k->dead < 0) {
            k->dead = (int16_t) nd;
            dead_show(k, s);
            return;
        }
        uint32_t first = (uint32_t) k->dead;
        if (first == nd) { /* the same dead key twice: the accent */
            n = zui_keymap_utf8(km, ZUI_KM_DEAD | first, buf, sizeof buf);
            dead_clear(k, s);
            emit_text(s, ZUI_EV_TEXT, buf);
            return;
        }
        uint32_t c = zui_keymap_combine(km, first, km->dead[nd].accent);
        if (c) {
            n = zui_km_utf8_put(buf, c);
            buf[n] = 0;
            dead_clear(k, s);
            emit_text(s, ZUI_EV_TEXT, buf);
            return;
        }
        /* the first accent, and the second waits */
        n = zui_keymap_utf8(km, ZUI_KM_DEAD | first, buf, sizeof buf);
        if (k->dead_preedit) emit_text(s, ZUI_EV_PREEDIT, "");
        emit_text(s, ZUI_EV_TEXT, buf);
        k->dead = (int16_t) nd;
        dead_show(k, s);
        return;
    }
    if (k->dead >= 0) {
        uint32_t d = (uint32_t) k->dead;
        uint32_t c = (!ZUI_KM_IS_STR(v) && !is_space) ? zui_keymap_combine(km, d, v) : 0;
        if (is_space) {
            n = zui_keymap_utf8(km, ZUI_KM_DEAD | d, buf, sizeof buf);
        } else if (c) {
            n = zui_km_utf8_put(buf, c);
            buf[n] = 0;
        } else {
            n = zui_keymap_utf8(km, ZUI_KM_DEAD | d, buf, sizeof buf);
            n += zui_keymap_utf8(km, v, buf + n, (uint32_t) sizeof buf - n);
        }
        dead_clear(k, s);
        emit_text(s, ZUI_EV_TEXT, buf);
        return;
    }
    n = zui_keymap_utf8(km, v, buf, sizeof buf);
    if (n) emit_text(s, ZUI_EV_TEXT, buf);
}

uint32_t zui_kbd_feed(zui_kbd_t *k, uint16_t code, bool down, bool repeat, uint32_t ms,
                      zui_event_t *out, uint32_t cap)
{
    sink_t s = {out, cap, 0, ms, 0};
    uint16_t mb = modifier_bit(k, code);
    if (mb) {
        if (down)
            k->held |= mb;
        else
            k->held &= (uint16_t) ~mb;
        return 0;
    }
    if (code == 58) { /* Caps Lock */
        if (down && !repeat) k->caps = !k->caps;
        return 0;
    }
    s.mods = zui_kbd_mods(k);
    bool no_text = (s.mods & (ZUI_MOD_CTRL | ZUI_MOD_ALT | ZUI_MOD_META)) != 0;
    bool shift = (s.mods & ZUI_MOD_SHIFT) != 0;
    bool altgr = (k->held & H_ALTGR) != 0;

    if (k->compose_code && code == k->compose_code) {
        if (down && k->dead >= 0) dead_clear(k, &s);
        emit_key(&s, down, ZUI_KEY_COMPOSE, repeat);
        return s.n;
    }

    int32_t pos = zui_kpos_from_evdev(code);
    uint32_t kp = keypad_char(code);
    if (pos >= 0 || kp) {
        uint16_t key = kp ? (uint16_t) kp : zui_keymap_shortcut(k->km, pos);
        emit_key(&s, down, key, down && repeat);
        if (!down || no_text) return s.n;
        uint32_t v = kp ? kp : zui_keymap_get(k->km, pos, shift, altgr, k->caps);
        type_value(k, &s, v, pos == ZUI_KP_SPACE, repeat);
        return s.n;
    }

    uint16_t key = special_key(code);
    if (key == ZUI_KEY_NONE) return 0;
    if (down && k->dead >= 0) {
        bool eat = key == ZUI_KEY_BACKSPACE || key == ZUI_KEY_ESCAPE;
        dead_clear(k, &s);
        if (eat) return s.n;
    }
    emit_key(&s, down, key, down && repeat);
    return s.n;
}

uint32_t zui_kbd_feed_queue(zui_kbd_t *k, zui_queue_t *q, uint16_t code, bool down, bool repeat,
                            uint32_t ms)
{
    zui_event_t ev[ZUI_KBD_EV_MAX];
    uint32_t n = zui_kbd_feed(k, code, down, repeat, ms, ev, ZUI_KBD_EV_MAX);
    for (uint32_t i = 0; i < n; i++) zui_push(q, &ev[i]);
    return n;
}
