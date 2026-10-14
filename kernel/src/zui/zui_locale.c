/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tables and test data derived from the Unicode Character Database / CLDR,
 * Copyright (c) Unicode, Inc., under the Unicode License v3
 * (https://www.unicode.org/license.txt). */
/* zui_locale.c — locales for zui (zui_locale.h L1..L10).
 *
 * The locale table below is generated from Unicode CLDR 48 (numbers,
 * ca-gregorian / ca-persian / ca-buddhist, plurals, weekData, likelySubtags);
 * the reader's words are this project's translations. */
#include "zui_locale.h"
#include "zui_bidi.h"

enum {
    ZUI_PLURAL_RULE_OTHER = 0,        /* zh ja ko th vi id yo                    */
    ZUI_PLURAL_RULE_ONE,              /* one: n = 1                              */
    ZUI_PLURAL_RULE_ONE_MILLION,      /* es it: one n = 1, many: millions        */
    ZUI_PLURAL_RULE_ZERO_ONE_MILLION, /* fr pt: one i = 0,1, many: millions     */
    ZUI_PLURAL_RULE_ZERO_ONE,         /* fa hi bn am: one i = 0 or n = 1         */
    ZUI_PLURAL_RULE_SLAVIC,           /* ru uk                                   */
    ZUI_PLURAL_RULE_POLISH,           /* pl                                      */
    ZUI_PLURAL_RULE_ARABIC,           /* ar                                      */
    ZUI_PLURAL_RULE_HEBREW            /* he: one, two                            */
};
enum { CAL_GREGORIAN = 0, CAL_PERSIAN, CAL_BUDDHIST };
enum {
    SYS_LATN = 0,
    SYS_ARAB,
    SYS_ARABEXT,
    SYS_BENG,
    SYS_DEVA,
    SYS_THAI,
    SYS_TAMLDEC,
    SYS_HANIDEC
};

typedef struct {
    uint8_t sys;
    const char *decimal, *group, *minus;
} numsys_t;

struct zui_locale {
    const char *tag, *name;
    uint8_t rtl, plural, first_day, calendar;
    uint8_t g1, g2, min_group; /* grouping: first group, the others, minimum digits */
    numsys_t num[2];           /* default, native */
    const char *date, *time, *am, *pm, *list;
    const char *const *words;
    const char *const (*counts)[ZUI_PLURAL_COUNT];
};

// clang-format off
static const char *const W_en[ZUI_WORD_COUNT] = {
    "group",
    "button",
    "list",
    "read-only text",
    "text area",
    "text field",
    "scroll area",
    "AI suggestion",
    "progress bar",
    "image",
    "separator",
    "heading",
    "check box",
    "region",
    "error",
    "menu",
    "menu item",
    "dialog",
    "option",
    "secure",
    "empty",
    "busy",
    "disabled",
    "checked",
    "not checked",
    "partly checked",
    "selected",
    "dimmed",
    "blank",
    "pending",
    "accepted",
    "rejected",
    "AI suggestion accepted",
    "AI suggestion rejected",
    "screen reader on",
    "screen reader off",
    "nothing on screen",
    "end",
    "top",
    "no next heading",
    "no previous heading",
    "no next landmark",
    "no previous landmark",
    "no next AI suggestion",
    "no previous AI suggestion",
    "no focus",
    "nothing to activate",
    "not a control",
    "no actions match",
    "accept with Command Return, or reject",
    "banner landmark",
    "navigation landmark",
    "main landmark",
    "search landmark",
    "complementary landmark",
    "status landmark",
    "verbosity low",
    "verbosity medium",
    "verbosity high",
    "Error: ",
    "shortcut",
    "from {x}",
    "by {x}",
    "about {x}",
    "heading level {n}",
    "{k} of {n}",
};
static const char *const C_en[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} item", 0, 0, 0, "{n} items"},
    {0, "{n} character", 0, 0, 0, "{n} characters"},
    {0, "{n} token", 0, 0, 0, "{n} tokens"},
    {0, 0, 0, 0, 0, "{n} percent"},
};
static const char *const W_es[ZUI_WORD_COUNT] = {
    "grupo",
    "botón",
    "lista",
    "texto de solo lectura",
    "área de texto",
    "campo de texto",
    "área de desplazamiento",
    "sugerencia de IA",
    "barra de progreso",
    "imagen",
    "separador",
    "encabezado",
    "casilla de verificación",
    "región",
    "error",
    "menú",
    "elemento de menú",
    "cuadro de diálogo",
    "opción",
    "protegido",
    "vacío",
    "ocupado",
    "no disponible",
    "marcado",
    "no marcado",
    "parcialmente marcado",
    "seleccionado",
    "atenuado",
    "en blanco",
    "pendiente",
    "aceptada",
    "rechazada",
    "sugerencia de IA aceptada",
    "sugerencia de IA rechazada",
    "lector de pantalla activado",
    "lector de pantalla desactivado",
    "no hay nada en la pantalla",
    "final",
    "inicio",
    "no hay más encabezados",
    "no hay encabezados anteriores",
    "no hay más puntos de referencia",
    "no hay puntos de referencia anteriores",
    "no hay más sugerencias de IA",
    "no hay sugerencias de IA anteriores",
    "sin foco",
    "nada que activar",
    "no es un control",
    "ninguna acción coincide",
    "acepta con Comando Retorno, o rechaza",
    "punto de referencia de banner",
    "punto de referencia de navegación",
    "punto de referencia principal",
    "punto de referencia de búsqueda",
    "punto de referencia complementario",
    "punto de referencia de estado",
    "nivel de detalle bajo",
    "nivel de detalle medio",
    "nivel de detalle alto",
    "Error: ",
    "atajo",
    "de {x}",
    "por {x}",
    "aproximadamente {x}",
    "encabezado de nivel {n}",
    "{k} de {n}",
};
static const char *const C_es[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} elemento", 0, 0, "{n} de elementos", "{n} elementos"},
    {0, "{n} carácter", 0, 0, "{n} de caracteres", "{n} caracteres"},
    {0, "{n} token", 0, 0, "{n} de tokens", "{n} tokens"},
    {0, 0, 0, 0, 0, "{n} por ciento"},
};
static const char *const W_fr[ZUI_WORD_COUNT] = {
    "groupe",
    "bouton",
    "liste",
    "texte en lecture seule",
    "zone de texte",
    "champ de texte",
    "zone de défilement",
    "suggestion de l’IA",
    "barre de progression",
    "image",
    "séparateur",
    "titre",
    "case à cocher",
    "région",
    "erreur",
    "menu",
    "élément de menu",
    "boîte de dialogue",
    "option",
    "protégé",
    "vide",
    "occupé",
    "indisponible",
    "coché",
    "non coché",
    "partiellement coché",
    "sélectionné",
    "estompé",
    "vide",
    "en attente",
    "acceptée",
    "refusée",
    "suggestion de l’IA acceptée",
    "suggestion de l’IA refusée",
    "lecteur d’écran activé",
    "lecteur d’écran désactivé",
    "rien à l’écran",
    "fin",
    "début",
    "aucun titre suivant",
    "aucun titre précédent",
    "aucun repère suivant",
    "aucun repère précédent",
    "aucune suggestion de l’IA suivante",
    "aucune suggestion de l’IA précédente",
    "aucun focus",
    "rien à activer",
    "pas une commande",
    "aucune action ne correspond",
    "accepter avec Commande Retour, ou refuser",
    "repère bannière",
    "repère navigation",
    "repère principal",
    "repère recherche",
    "repère complémentaire",
    "repère état",
    "verbosité faible",
    "verbosité moyenne",
    "verbosité élevée",
    "Erreur\u00A0: ",
    "raccourci",
    "de {x}",
    "par {x}",
    "environ {x}",
    "titre de niveau {n}",
    "{k} sur {n}",
};
static const char *const C_fr[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} élément", 0, 0, "{n} d’éléments", "{n} éléments"},
    {0, "{n} caractère", 0, 0, "{n} de caractères", "{n} caractères"},
    {0, "{n} jeton", 0, 0, "{n} de jetons", "{n} jetons"},
    {0, 0, 0, 0, 0, "{n} pour cent"},
};
static const char *const W_de[ZUI_WORD_COUNT] = {
    "Gruppe",
    "Schaltfläche",
    "Liste",
    "schreibgeschützter Text",
    "Textbereich",
    "Textfeld",
    "Bildlaufbereich",
    "KI-Vorschlag",
    "Fortschrittsbalken",
    "Bild",
    "Trennlinie",
    "Überschrift",
    "Kontrollkästchen",
    "Bereich",
    "Fehler",
    "Menü",
    "Menüeintrag",
    "Dialogfeld",
    "Option",
    "geschützt",
    "leer",
    "beschäftigt",
    "nicht verfügbar",
    "aktiviert",
    "nicht aktiviert",
    "teilweise aktiviert",
    "ausgewählt",
    "abgeblendet",
    "leer",
    "ausstehend",
    "angenommen",
    "abgelehnt",
    "KI-Vorschlag angenommen",
    "KI-Vorschlag abgelehnt",
    "Bildschirmleser ein",
    "Bildschirmleser aus",
    "nichts auf dem Bildschirm",
    "Ende",
    "Anfang",
    "keine nächste Überschrift",
    "keine vorherige Überschrift",
    "kein nächster Orientierungspunkt",
    "kein vorheriger Orientierungspunkt",
    "kein nächster KI-Vorschlag",
    "kein vorheriger KI-Vorschlag",
    "kein Fokus",
    "nichts zu aktivieren",
    "kein Steuerelement",
    "keine passenden Aktionen",
    "mit Befehl Zeilenschalter annehmen oder ablehnen",
    "Banner-Orientierungspunkt",
    "Navigations-Orientierungspunkt",
    "Haupt-Orientierungspunkt",
    "Such-Orientierungspunkt",
    "ergänzender Orientierungspunkt",
    "Status-Orientierungspunkt",
    "Ausführlichkeit niedrig",
    "Ausführlichkeit mittel",
    "Ausführlichkeit hoch",
    "Fehler: ",
    "Tastenkürzel",
    "von {x}",
    "von {x}",
    "etwa {x}",
    "Überschrift Ebene {n}",
    "{k} von {n}",
};
static const char *const C_de[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} Element", 0, 0, 0, "{n} Elemente"},
    {0, "{n} Zeichen", 0, 0, 0, "{n} Zeichen"},
    {0, "{n} Token", 0, 0, 0, "{n} Token"},
    {0, 0, 0, 0, 0, "{n} Prozent"},
};
static const char *const W_pt[ZUI_WORD_COUNT] = {
    "grupo",
    "botão",
    "lista",
    "texto somente leitura",
    "área de texto",
    "campo de texto",
    "área de rolagem",
    "sugestão de IA",
    "barra de progresso",
    "imagem",
    "separador",
    "título",
    "caixa de seleção",
    "região",
    "erro",
    "menu",
    "item de menu",
    "caixa de diálogo",
    "opção",
    "protegido",
    "vazio",
    "ocupado",
    "indisponível",
    "marcado",
    "desmarcado",
    "parcialmente marcado",
    "selecionado",
    "esmaecido",
    "em branco",
    "pendente",
    "aceita",
    "rejeitada",
    "sugestão de IA aceita",
    "sugestão de IA rejeitada",
    "leitor de tela ativado",
    "leitor de tela desativado",
    "nada na tela",
    "fim",
    "início",
    "nenhum título seguinte",
    "nenhum título anterior",
    "nenhum ponto de referência seguinte",
    "nenhum ponto de referência anterior",
    "nenhuma sugestão de IA seguinte",
    "nenhuma sugestão de IA anterior",
    "sem foco",
    "nada para ativar",
    "não é um controle",
    "nenhuma ação corresponde",
    "aceite com Command Return, ou rejeite",
    "ponto de referência de banner",
    "ponto de referência de navegação",
    "ponto de referência principal",
    "ponto de referência de pesquisa",
    "ponto de referência complementar",
    "ponto de referência de status",
    "detalhamento baixo",
    "detalhamento médio",
    "detalhamento alto",
    "Erro: ",
    "atalho",
    "de {x}",
    "por {x}",
    "cerca de {x}",
    "título de nível {n}",
    "{k} de {n}",
};
static const char *const C_pt[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} item", 0, 0, "{n} de itens", "{n} itens"},
    {0, "{n} caractere", 0, 0, "{n} de caracteres", "{n} caracteres"},
    {0, "{n} token", 0, 0, "{n} de tokens", "{n} tokens"},
    {0, 0, 0, 0, 0, "{n} por cento"},
};
static const char *const W_it[ZUI_WORD_COUNT] = {
    "gruppo",
    "pulsante",
    "elenco",
    "testo di sola lettura",
    "area di testo",
    "campo di testo",
    "area di scorrimento",
    "suggerimento IA",
    "barra di avanzamento",
    "immagine",
    "separatore",
    "intestazione",
    "casella di controllo",
    "regione",
    "errore",
    "menu",
    "voce di menu",
    "finestra di dialogo",
    "opzione",
    "protetto",
    "vuoto",
    "occupato",
    "non disponibile",
    "spuntato",
    "non spuntato",
    "parzialmente spuntato",
    "selezionato",
    "attenuato",
    "vuoto",
    "in attesa",
    "accettato",
    "rifiutato",
    "suggerimento IA accettato",
    "suggerimento IA rifiutato",
    "lettore schermo attivato",
    "lettore schermo disattivato",
    "niente sullo schermo",
    "fine",
    "inizio",
    "nessuna intestazione successiva",
    "nessuna intestazione precedente",
    "nessun punto di riferimento successivo",
    "nessun punto di riferimento precedente",
    "nessun suggerimento IA successivo",
    "nessun suggerimento IA precedente",
    "nessun focus",
    "niente da attivare",
    "non è un controllo",
    "nessuna azione corrispondente",
    "accetta con Comando Invio, o rifiuta",
    "punto di riferimento banner",
    "punto di riferimento navigazione",
    "punto di riferimento principale",
    "punto di riferimento ricerca",
    "punto di riferimento complementare",
    "punto di riferimento stato",
    "livello di dettaglio basso",
    "livello di dettaglio medio",
    "livello di dettaglio alto",
    "Errore: ",
    "scorciatoia",
    "da {x}",
    "di {x}",
    "circa {x}",
    "intestazione di livello {n}",
    "{k} di {n}",
};
static const char *const C_it[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} elemento", 0, 0, "{n} di elementi", "{n} elementi"},
    {0, "{n} carattere", 0, 0, "{n} di caratteri", "{n} caratteri"},
    {0, "{n} token", 0, 0, "{n} di token", "{n} token"},
    {0, 0, 0, 0, 0, "{n} per cento"},
};
static const char *const W_ru[ZUI_WORD_COUNT] = {
    "группа",
    "кнопка",
    "список",
    "текст только для чтения",
    "текстовая область",
    "текстовое поле",
    "область прокрутки",
    "предложение ИИ",
    "индикатор выполнения",
    "изображение",
    "разделитель",
    "заголовок",
    "флажок",
    "область",
    "ошибка",
    "меню",
    "пункт меню",
    "диалог",
    "вариант",
    "защищено",
    "пусто",
    "занято",
    "недоступно",
    "отмечено",
    "не отмечено",
    "частично отмечено",
    "выбрано",
    "затенено",
    "пусто",
    "ожидает",
    "принято",
    "отклонено",
    "предложение ИИ принято",
    "предложение ИИ отклонено",
    "чтение экрана включено",
    "чтение экрана выключено",
    "на экране ничего нет",
    "конец",
    "начало",
    "нет следующего заголовка",
    "нет предыдущего заголовка",
    "нет следующего ориентира",
    "нет предыдущего ориентира",
    "нет следующего предложения ИИ",
    "нет предыдущего предложения ИИ",
    "нет фокуса",
    "нечего активировать",
    "не элемент управления",
    "нет подходящих действий",
    "принять: Command Return, или отклонить",
    "ориентир баннер",
    "ориентир навигация",
    "основной ориентир",
    "ориентир поиск",
    "дополнительный ориентир",
    "ориентир состояние",
    "подробность низкая",
    "подробность средняя",
    "подробность высокая",
    "Ошибка: ",
    "сочетание клавиш",
    "от {x}",
    "от {x}",
    "примерно {x}",
    "заголовок уровня {n}",
    "{k} из {n}",
};
static const char *const C_ru[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} элемент", 0, "{n} элемента", "{n} элементов", "{n} элемента"},
    {0, "{n} символ", 0, "{n} символа", "{n} символов", "{n} символа"},
    {0, "{n} токен", 0, "{n} токена", "{n} токенов", "{n} токена"},
    {0, "{n} процент", 0, "{n} процента", "{n} процентов", "{n} процента"},
};
static const char *const W_uk[ZUI_WORD_COUNT] = {
    "група",
    "кнопка",
    "список",
    "текст лише для читання",
    "текстова область",
    "текстове поле",
    "область прокручування",
    "пропозиція ШІ",
    "індикатор перебігу",
    "зображення",
    "роздільник",
    "заголовок",
    "прапорець",
    "область",
    "помилка",
    "меню",
    "пункт меню",
    "діалог",
    "варіант",
    "захищено",
    "порожньо",
    "зайнято",
    "недоступно",
    "позначено",
    "не позначено",
    "частково позначено",
    "вибрано",
    "затемнено",
    "порожньо",
    "очікує",
    "прийнято",
    "відхилено",
    "пропозицію ШІ прийнято",
    "пропозицію ШІ відхилено",
    "читання екрана увімкнено",
    "читання екрана вимкнено",
    "на екрані нічого немає",
    "кінець",
    "початок",
    "немає наступного заголовка",
    "немає попереднього заголовка",
    "немає наступного орієнтира",
    "немає попереднього орієнтира",
    "немає наступної пропозиції ШІ",
    "немає попередньої пропозиції ШІ",
    "немає фокуса",
    "нічого активувати",
    "не елемент керування",
    "немає відповідних дій",
    "прийняти: Command Return, або відхилити",
    "орієнтир банер",
    "орієнтир навігація",
    "основний орієнтир",
    "орієнтир пошук",
    "додатковий орієнтир",
    "орієнтир стан",
    "докладність низька",
    "докладність середня",
    "докладність висока",
    "Помилка: ",
    "сполучення клавіш",
    "від {x}",
    "від {x}",
    "приблизно {x}",
    "заголовок рівня {n}",
    "{k} з {n}",
};
static const char *const C_uk[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} елемент", 0, "{n} елементи", "{n} елементів", "{n} елемента"},
    {0, "{n} символ", 0, "{n} символи", "{n} символів", "{n} символу"},
    {0, "{n} токен", 0, "{n} токени", "{n} токенів", "{n} токена"},
    {0, "{n} відсоток", 0, "{n} відсотки", "{n} відсотків", "{n} відсотка"},
};
static const char *const W_pl[ZUI_WORD_COUNT] = {
    "grupa",
    "przycisk",
    "lista",
    "tekst tylko do odczytu",
    "obszar tekstowy",
    "pole tekstowe",
    "obszar przewijania",
    "sugestia AI",
    "pasek postępu",
    "obraz",
    "separator",
    "nagłówek",
    "pole wyboru",
    "region",
    "błąd",
    "menu",
    "element menu",
    "okno dialogowe",
    "opcja",
    "zabezpieczone",
    "puste",
    "zajęte",
    "niedostępne",
    "zaznaczone",
    "niezaznaczone",
    "częściowo zaznaczone",
    "wybrane",
    "przyciemnione",
    "puste",
    "oczekuje",
    "zaakceptowana",
    "odrzucona",
    "sugestia AI zaakceptowana",
    "sugestia AI odrzucona",
    "czytnik ekranu włączony",
    "czytnik ekranu wyłączony",
    "nic na ekranie",
    "koniec",
    "początek",
    "brak następnego nagłówka",
    "brak poprzedniego nagłówka",
    "brak następnego punktu orientacyjnego",
    "brak poprzedniego punktu orientacyjnego",
    "brak następnej sugestii AI",
    "brak poprzedniej sugestii AI",
    "brak fokusu",
    "nic do aktywowania",
    "to nie jest kontrolka",
    "brak pasujących akcji",
    "zaakceptuj, naciskając Command Return, lub odrzuć",
    "punkt orientacyjny baner",
    "punkt orientacyjny nawigacja",
    "główny punkt orientacyjny",
    "punkt orientacyjny wyszukiwanie",
    "uzupełniający punkt orientacyjny",
    "punkt orientacyjny stan",
    "szczegółowość niska",
    "szczegółowość średnia",
    "szczegółowość wysoka",
    "Błąd: ",
    "skrót",
    "od {x}",
    "od {x}",
    "mniej więcej {x}",
    "nagłówek poziomu {n}",
    "{k} z {n}",
};
static const char *const C_pl[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} element", 0, "{n} elementy", "{n} elementów", "{n} elementu"},
    {0, "{n} znak", 0, "{n} znaki", "{n} znaków", "{n} znaku"},
    {0, "{n} token", 0, "{n} tokeny", "{n} tokenów", "{n} tokena"},
    {0, 0, 0, 0, 0, "{n} procent"},
};
static const char *const W_ar[ZUI_WORD_COUNT] = {
    "مجموعة",
    "زر",
    "قائمة",
    "نص للقراءة فقط",
    "منطقة نص",
    "حقل نص",
    "منطقة تمرير",
    "اقتراح الذكاء الاصطناعي",
    "شريط التقدم",
    "صورة",
    "فاصل",
    "عنوان",
    "مربع اختيار",
    "منطقة",
    "خطأ",
    "قائمة أوامر",
    "عنصر قائمة",
    "مربع حوار",
    "خيار",
    "محمي",
    "فارغ",
    "مشغول",
    "غير متاح",
    "مؤشر عليه",
    "غير مؤشر عليه",
    "مؤشر عليه جزئيًا",
    "محدد",
    "باهت",
    "فارغ",
    "قيد الانتظار",
    "مقبول",
    "مرفوض",
    "تم قبول اقتراح الذكاء الاصطناعي",
    "تم رفض اقتراح الذكاء الاصطناعي",
    "قارئ الشاشة يعمل",
    "قارئ الشاشة متوقف",
    "لا شيء على الشاشة",
    "النهاية",
    "البداية",
    "لا يوجد عنوان تالٍ",
    "لا يوجد عنوان سابق",
    "لا يوجد معلم تالٍ",
    "لا يوجد معلم سابق",
    "لا يوجد اقتراح تالٍ من الذكاء الاصطناعي",
    "لا يوجد اقتراح سابق من الذكاء الاصطناعي",
    "لا يوجد تركيز",
    "لا شيء لتفعيله",
    "ليس عنصر تحكم",
    "لا توجد إجراءات مطابقة",
    "اقبل باستخدام Command Return، أو ارفض",
    "معلم الشعار",
    "معلم التنقل",
    "المعلم الرئيسي",
    "معلم البحث",
    "معلم تكميلي",
    "معلم الحالة",
    "مستوى التفاصيل منخفض",
    "مستوى التفاصيل متوسط",
    "مستوى التفاصيل مرتفع",
    "خطأ: ",
    "اختصار",
    "من {x}",
    "بواسطة {x}",
    "حوالي {x}",
    "عنوان المستوى {n}",
    "{k} من {n}",
};
static const char *const C_ar[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {"{n} عنصر", "عنصر واحد", "عنصران", "{n} عناصر", "{n} عنصرًا", "{n} عنصر"},
    {"{n} حرف", "حرف واحد", "حرفان", "{n} أحرف", "{n} حرفًا", "{n} حرف"},
    {"{n} رمز", "رمز واحد", "رمزان", "{n} رموز", "{n} رمزًا", "{n} رمز"},
    {0, 0, 0, 0, 0, "{n} بالمئة"},
};
static const char *const W_he[ZUI_WORD_COUNT] = {
    "קבוצה",
    "לחצן",
    "רשימה",
    "טקסט לקריאה בלבד",
    "אזור טקסט",
    "שדה טקסט",
    "אזור גלילה",
    "הצעת בינה מלאכותית",
    "פס התקדמות",
    "תמונה",
    "מפריד",
    "כותרת",
    "תיבת סימון",
    "אזור",
    "שגיאה",
    "תפריט",
    "פריט תפריט",
    "תיבת דו-שיח",
    "אפשרות",
    "מאובטח",
    "ריק",
    "עסוק",
    "לא זמין",
    "מסומן",
    "לא מסומן",
    "מסומן חלקית",
    "נבחר",
    "מעומעם",
    "ריק",
    "ממתין",
    "התקבלה",
    "נדחתה",
    "הצעת הבינה המלאכותית התקבלה",
    "הצעת הבינה המלאכותית נדחתה",
    "קורא המסך מופעל",
    "קורא המסך כבוי",
    "אין דבר על המסך",
    "סוף",
    "התחלה",
    "אין כותרת הבאה",
    "אין כותרת קודמת",
    "אין ציון דרך הבא",
    "אין ציון דרך קודם",
    "אין הצעת בינה מלאכותית הבאה",
    "אין הצעת בינה מלאכותית קודמת",
    "אין מיקוד",
    "אין מה להפעיל",
    "לא פקד",
    "אין פעולות תואמות",
    "אשר עם Command Return, או דחה",
    "ציון דרך באנר",
    "ציון דרך ניווט",
    "ציון דרך ראשי",
    "ציון דרך חיפוש",
    "ציון דרך משלים",
    "ציון דרך מצב",
    "רמת פירוט נמוכה",
    "רמת פירוט בינונית",
    "רמת פירוט גבוהה",
    "שגיאה: ",
    "קיצור מקשים",
    "מאת {x}",
    "על ידי {x}",
    "בערך {x}",
    "כותרת רמה {n}",
    "{k} מתוך {n}",
};
static const char *const C_he[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "פריט אחד", "שני פריטים", 0, 0, "{n} פריטים"},
    {0, "תו אחד", "שני תווים", 0, 0, "{n} תווים"},
    {0, "אסימון אחד", "שני אסימונים", 0, 0, "{n} אסימונים"},
    {0, 0, 0, 0, 0, "{n} אחוז"},
};
static const char *const W_fa[ZUI_WORD_COUNT] = {
    "گروه",
    "دکمه",
    "فهرست",
    "متن فقط خواندنی",
    "ناحیه متن",
    "فیلد متن",
    "ناحیه پیمایش",
    "پیشنهاد هوش مصنوعی",
    "نوار پیشرفت",
    "تصویر",
    "جداکننده",
    "سرعنوان",
    "کادر انتخاب",
    "ناحیه",
    "خطا",
    "منو",
    "مورد منو",
    "کادر گفتگو",
    "گزینه",
    "امن",
    "خالی",
    "مشغول",
    "غیرفعال",
    "علامت\u200Cخورده",
    "بدون علامت",
    "تا حدی علامت\u200Cخورده",
    "انتخاب\u200Cشده",
    "کم\u200Cرنگ",
    "خالی",
    "در انتظار",
    "پذیرفته شد",
    "رد شد",
    "پیشنهاد هوش مصنوعی پذیرفته شد",
    "پیشنهاد هوش مصنوعی رد شد",
    "صفحه\u200Cخوان روشن",
    "صفحه\u200Cخوان خاموش",
    "چیزی روی صفحه نیست",
    "پایان",
    "ابتدا",
    "سرعنوان بعدی وجود ندارد",
    "سرعنوان قبلی وجود ندارد",
    "نشانه بعدی وجود ندارد",
    "نشانه قبلی وجود ندارد",
    "پیشنهاد بعدی هوش مصنوعی وجود ندارد",
    "پیشنهاد قبلی هوش مصنوعی وجود ندارد",
    "بدون فوکوس",
    "چیزی برای فعال\u200Cسازی نیست",
    "کنترل نیست",
    "هیچ کنشی مطابقت ندارد",
    "با Command Return بپذیرید، یا رد کنید",
    "نشانه سرصفحه",
    "نشانه پیمایش",
    "نشانه اصلی",
    "نشانه جستجو",
    "نشانه تکمیلی",
    "نشانه وضعیت",
    "میزان جزئیات کم",
    "میزان جزئیات متوسط",
    "میزان جزئیات زیاد",
    "خطا: ",
    "میان\u200Cبر",
    "از {x}",
    "توسط {x}",
    "حدود {x}",
    "سرعنوان سطح {n}",
    "{k} از {n}",
};
static const char *const C_fa[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, 0, 0, 0, 0, "{n} مورد"},
    {0, 0, 0, 0, 0, "{n} نویسه"},
    {0, 0, 0, 0, 0, "{n} توکن"},
    {0, 0, 0, 0, 0, "{n} درصد"},
};
static const char *const W_ur[ZUI_WORD_COUNT] = {
    "گروپ",
    "بٹن",
    "فہرست",
    "صرف پڑھنے کے لیے متن",
    "متن کا علاقہ",
    "متن کا فیلڈ",
    "اسکرول کا علاقہ",
    "AI تجویز",
    "پیش رفت بار",
    "تصویر",
    "علیحدہ کار",
    "سرخی",
    "چیک باکس",
    "علاقہ",
    "خرابی",
    "مینو",
    "مینو آئٹم",
    "ڈائیلاگ",
    "اختیار",
    "محفوظ",
    "خالی",
    "مصروف",
    "غیر فعال",
    "نشان زد",
    "نشان زد نہیں",
    "جزوی طور پر نشان زد",
    "منتخب",
    "مدھم",
    "خالی",
    "زیر التواء",
    "قبول کر لی گئی",
    "مسترد کر دی گئی",
    "AI تجویز قبول کر لی گئی",
    "AI تجویز مسترد کر دی گئی",
    "اسکرین ریڈر آن",
    "اسکرین ریڈر آف",
    "اسکرین پر کچھ نہیں",
    "اختتام",
    "آغاز",
    "کوئی اگلی سرخی نہیں",
    "کوئی پچھلی سرخی نہیں",
    "کوئی اگلا لینڈ مارک نہیں",
    "کوئی پچھلا لینڈ مارک نہیں",
    "کوئی اگلی AI تجویز نہیں",
    "کوئی پچھلی AI تجویز نہیں",
    "کوئی فوکس نہیں",
    "فعال کرنے کو کچھ نہیں",
    "کنٹرول نہیں",
    "کوئی کارروائی مماثل نہیں",
    "Command Return سے قبول کریں، یا مسترد کریں",
    "بینر لینڈ مارک",
    "نیویگیشن لینڈ مارک",
    "مرکزی لینڈ مارک",
    "تلاش لینڈ مارک",
    "تکمیلی لینڈ مارک",
    "اسٹیٹس لینڈ مارک",
    "تفصیل کی سطح کم",
    "تفصیل کی سطح درمیانی",
    "تفصیل کی سطح زیادہ",
    "خرابی: ",
    "شارٹ کٹ",
    "{x} سے",
    "{x} کی جانب سے",
    "تقریباً {x}",
    "سرخی سطح {n}",
    "{n} میں سے {k}",
};
static const char *const C_ur[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} آئٹم", 0, 0, 0, "{n} آئٹمز"},
    {0, "{n} حرف", 0, 0, 0, "{n} حروف"},
    {0, "{n} ٹوکن", 0, 0, 0, "{n} ٹوکنز"},
    {0, 0, 0, 0, 0, "{n} فیصد"},
};
static const char *const W_hi[ZUI_WORD_COUNT] = {
    "समूह",
    "बटन",
    "सूची",
    "केवल पढ़ने योग्य पाठ",
    "पाठ क्षेत्र",
    "पाठ फ़ील्ड",
    "स्क्रॉल क्षेत्र",
    "AI सुझाव",
    "प्रगति पट्टी",
    "छवि",
    "विभाजक",
    "शीर्षक",
    "चेक बॉक्स",
    "क्षेत्र",
    "त्रुटि",
    "मेनू",
    "मेनू आइटम",
    "संवाद",
    "विकल्प",
    "सुरक्षित",
    "खाली",
    "व्यस्त",
    "अक्षम",
    "चेक किया गया",
    "चेक नहीं किया गया",
    "आंशिक रूप से चेक किया गया",
    "चयनित",
    "धुंधला",
    "रिक्त",
    "लंबित",
    "स्वीकृत",
    "अस्वीकृत",
    "AI सुझाव स्वीकृत",
    "AI सुझाव अस्वीकृत",
    "स्क्रीन रीडर चालू",
    "स्क्रीन रीडर बंद",
    "स्क्रीन पर कुछ नहीं",
    "अंत",
    "शुरुआत",
    "कोई अगला शीर्षक नहीं",
    "कोई पिछला शीर्षक नहीं",
    "कोई अगला लैंडमार्क नहीं",
    "कोई पिछला लैंडमार्क नहीं",
    "कोई अगला AI सुझाव नहीं",
    "कोई पिछला AI सुझाव नहीं",
    "कोई फ़ोकस नहीं",
    "सक्रिय करने के लिए कुछ नहीं",
    "नियंत्रण नहीं है",
    "कोई क्रिया मेल नहीं खाती",
    "Command Return से स्वीकार करें, या अस्वीकार करें",
    "बैनर लैंडमार्क",
    "नेविगेशन लैंडमार्क",
    "मुख्य लैंडमार्क",
    "खोज लैंडमार्क",
    "पूरक लैंडमार्क",
    "स्थिति लैंडमार्क",
    "विस्तार स्तर निम्न",
    "विस्तार स्तर मध्यम",
    "विस्तार स्तर उच्च",
    "त्रुटि: ",
    "शॉर्टकट",
    "{x} से",
    "{x} द्वारा",
    "लगभग {x}",
    "शीर्षक स्तर {n}",
    "{n} में से {k}",
};
static const char *const C_hi[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} आइटम", 0, 0, 0, "{n} आइटम"},
    {0, "{n} वर्ण", 0, 0, 0, "{n} वर्ण"},
    {0, "{n} टोकन", 0, 0, 0, "{n} टोकन"},
    {0, 0, 0, 0, 0, "{n} प्रतिशत"},
};
static const char *const W_bn[ZUI_WORD_COUNT] = {
    "গোষ্ঠী",
    "বোতাম",
    "তালিকা",
    "শুধু পড়ার জন্য পাঠ্য",
    "পাঠ্য এলাকা",
    "পাঠ্য ক্ষেত্র",
    "স্ক্রল এলাকা",
    "AI পরামর্শ",
    "অগ্রগতি দণ্ড",
    "ছবি",
    "বিভাজক",
    "শিরোনাম",
    "চেক বক্স",
    "অঞ্চল",
    "ত্রুটি",
    "মেনু",
    "মেনু আইটেম",
    "ডায়ালগ",
    "বিকল্প",
    "সুরক্ষিত",
    "খালি",
    "ব্যস্ত",
    "নিষ্ক্রিয়",
    "চেক করা",
    "চেক করা নয়",
    "আংশিক চেক করা",
    "নির্বাচিত",
    "অনুজ্জ্বল",
    "ফাঁকা",
    "মুলতুবি",
    "গৃহীত",
    "প্রত্যাখ্যাত",
    "AI পরামর্শ গৃহীত",
    "AI পরামর্শ প্রত্যাখ্যাত",
    "স্ক্রিন রিডার চালু",
    "স্ক্রিন রিডার বন্ধ",
    "স্ক্রিনে কিছু নেই",
    "শেষ",
    "শুরু",
    "পরবর্তী কোনো শিরোনাম নেই",
    "পূর্ববর্তী কোনো শিরোনাম নেই",
    "পরবর্তী কোনো ল্যান্ডমার্ক নেই",
    "পূর্ববর্তী কোনো ল্যান্ডমার্ক নেই",
    "পরবর্তী কোনো AI পরামর্শ নেই",
    "পূর্ববর্তী কোনো AI পরামর্শ নেই",
    "কোনো ফোকাস নেই",
    "সক্রিয় করার কিছু নেই",
    "নিয়ন্ত্রণ নয়",
    "কোনো কাজ মেলেনি",
    "Command Return দিয়ে গ্রহণ করুন, অথবা প্রত্যাখ্যান করুন",
    "ব্যানার ল্যান্ডমার্ক",
    "নেভিগেশন ল্যান্ডমার্ক",
    "প্রধান ল্যান্ডমার্ক",
    "অনুসন্ধান ল্যান্ডমার্ক",
    "পরিপূরক ল্যান্ডমার্ক",
    "অবস্থা ল্যান্ডমার্ক",
    "বিস্তারিত স্তর নিম্ন",
    "বিস্তারিত স্তর মাঝারি",
    "বিস্তারিত স্তর উচ্চ",
    "ত্রুটি: ",
    "শর্টকাট",
    "{x} থেকে",
    "{x} দ্বারা",
    "প্রায় {x}",
    "শিরোনাম স্তর {n}",
    "{n}টির মধ্যে {k}",
};
static const char *const C_bn[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n}টি আইটেম", 0, 0, 0, "{n}টি আইটেম"},
    {0, "{n}টি অক্ষর", 0, 0, 0, "{n}টি অক্ষর"},
    {0, "{n}টি টোকেন", 0, 0, 0, "{n}টি টোকেন"},
    {0, 0, 0, 0, 0, "{n} শতাংশ"},
};
static const char *const W_ta[ZUI_WORD_COUNT] = {
    "குழு",
    "பொத்தான்",
    "பட்டியல்",
    "படிக்க மட்டுமான உரை",
    "உரைப் பகுதி",
    "உரைப் புலம்",
    "உருட்டும் பகுதி",
    "AI பரிந்துரை",
    "முன்னேற்றப் பட்டை",
    "படம்",
    "பிரிப்பான்",
    "தலைப்பு",
    "தேர்வுப்பெட்டி",
    "மண்டலம்",
    "பிழை",
    "மெனு",
    "மெனு உருப்படி",
    "உரையாடல் பெட்டி",
    "விருப்பம்",
    "பாதுகாப்பானது",
    "காலி",
    "பணியில் உள்ளது",
    "முடக்கப்பட்டது",
    "தேர்வுசெய்யப்பட்டது",
    "தேர்வுசெய்யப்படவில்லை",
    "பகுதியாகத் தேர்வுசெய்யப்பட்டது",
    "தேர்ந்தெடுக்கப்பட்டது",
    "மங்கலானது",
    "வெற்று",
    "நிலுவையில் உள்ளது",
    "ஏற்கப்பட்டது",
    "நிராகரிக்கப்பட்டது",
    "AI பரிந்துரை ஏற்கப்பட்டது",
    "AI பரிந்துரை நிராகரிக்கப்பட்டது",
    "திரைப் படிப்பான் இயக்கத்தில்",
    "திரைப் படிப்பான் முடக்கத்தில்",
    "திரையில் எதுவும் இல்லை",
    "முடிவு",
    "மேலே",
    "அடுத்த தலைப்பு இல்லை",
    "முந்தைய தலைப்பு இல்லை",
    "அடுத்த அடையாளக்குறி இல்லை",
    "முந்தைய அடையாளக்குறி இல்லை",
    "அடுத்த AI பரிந்துரை இல்லை",
    "முந்தைய AI பரிந்துரை இல்லை",
    "கவனம் இல்லை",
    "செயல்படுத்த எதுவும் இல்லை",
    "கட்டுப்பாடு அல்ல",
    "பொருந்தும் செயல்கள் இல்லை",
    "Command Return மூலம் ஏற்கவும், அல்லது நிராகரிக்கவும்",
    "பேனர் அடையாளக்குறி",
    "வழிசெலுத்தல் அடையாளக்குறி",
    "முதன்மை அடையாளக்குறி",
    "தேடல் அடையாளக்குறி",
    "துணை அடையாளக்குறி",
    "நிலை அடையாளக்குறி",
    "விவர நிலை குறைவு",
    "விவர நிலை நடுத்தரம்",
    "விவர நிலை அதிகம்",
    "பிழை: ",
    "குறுக்குவழி",
    "{x} இலிருந்து",
    "{x} மூலம்",
    "சுமார் {x}",
    "தலைப்பு நிலை {n}",
    "{n}இல் {k}",
};
static const char *const C_ta[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} உருப்படி", 0, 0, 0, "{n} உருப்படிகள்"},
    {0, "{n} எழுத்து", 0, 0, 0, "{n} எழுத்துகள்"},
    {0, "{n} டோக்கன்", 0, 0, 0, "{n} டோக்கன்கள்"},
    {0, 0, 0, 0, 0, "{n} சதவீதம்"},
};
static const char *const W_zh_Hans[ZUI_WORD_COUNT] = {
    "组",
    "按钮",
    "列表",
    "只读文本",
    "文本区域",
    "文本框",
    "滚动区域",
    "AI 建议",
    "进度条",
    "图像",
    "分隔符",
    "标题",
    "复选框",
    "区域",
    "错误",
    "菜单",
    "菜单项",
    "对话框",
    "选项",
    "安全",
    "空",
    "忙碌",
    "不可用",
    "已勾选",
    "未勾选",
    "部分勾选",
    "已选择",
    "变暗",
    "空白",
    "待处理",
    "已接受",
    "已拒绝",
    "AI 建议已接受",
    "AI 建议已拒绝",
    "屏幕阅读器已开启",
    "屏幕阅读器已关闭",
    "屏幕上没有内容",
    "末尾",
    "顶部",
    "没有下一个标题",
    "没有上一个标题",
    "没有下一个地标",
    "没有上一个地标",
    "没有下一个 AI 建议",
    "没有上一个 AI 建议",
    "无焦点",
    "没有可激活的内容",
    "不是控件",
    "没有匹配的操作",
    "按 Command Return 接受，或拒绝",
    "横幅地标",
    "导航地标",
    "主要地标",
    "搜索地标",
    "补充地标",
    "状态地标",
    "详细程度 低",
    "详细程度 中",
    "详细程度 高",
    "错误：",
    "快捷键",
    "来自 {x}",
    "由 {x} 提供",
    "约 {x}",
    "标题级别 {n}",
    "第 {k} 项，共 {n} 项",
};
static const char *const C_zh_Hans[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, 0, 0, 0, 0, "{n} 项"},
    {0, 0, 0, 0, 0, "{n} 个字符"},
    {0, 0, 0, 0, 0, "{n} 个词元"},
    {0, 0, 0, 0, 0, "百分之 {n}"},
};
static const char *const W_zh_Hant[ZUI_WORD_COUNT] = {
    "群組",
    "按鈕",
    "清單",
    "唯讀文字",
    "文字區域",
    "文字欄位",
    "捲動區域",
    "AI 建議",
    "進度列",
    "影像",
    "分隔線",
    "標題",
    "核取方塊",
    "區域",
    "錯誤",
    "選單",
    "選單項目",
    "對話框",
    "選項",
    "安全",
    "空白",
    "忙碌中",
    "無法使用",
    "已勾選",
    "未勾選",
    "部分勾選",
    "已選取",
    "變暗",
    "空白",
    "待處理",
    "已接受",
    "已拒絕",
    "AI 建議已接受",
    "AI 建議已拒絕",
    "螢幕閱讀器已開啟",
    "螢幕閱讀器已關閉",
    "螢幕上沒有內容",
    "結尾",
    "頂端",
    "沒有下一個標題",
    "沒有上一個標題",
    "沒有下一個地標",
    "沒有上一個地標",
    "沒有下一個 AI 建議",
    "沒有上一個 AI 建議",
    "沒有焦點",
    "沒有可啟用的項目",
    "不是控制項",
    "沒有符合的動作",
    "按 Command Return 接受，或拒絕",
    "橫幅地標",
    "導覽地標",
    "主要地標",
    "搜尋地標",
    "補充地標",
    "狀態地標",
    "詳細程度 低",
    "詳細程度 中",
    "詳細程度 高",
    "錯誤：",
    "快速鍵",
    "來自 {x}",
    "由 {x} 提供",
    "約 {x}",
    "標題層級 {n}",
    "第 {k} 項，共 {n} 項",
};
static const char *const C_zh_Hant[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, 0, 0, 0, 0, "{n} 個項目"},
    {0, 0, 0, 0, 0, "{n} 個字元"},
    {0, 0, 0, 0, 0, "{n} 個詞元"},
    {0, 0, 0, 0, 0, "百分之 {n}"},
};
static const char *const W_ja[ZUI_WORD_COUNT] = {
    "グループ",
    "ボタン",
    "リスト",
    "読み取り専用テキスト",
    "テキストエリア",
    "テキストフィールド",
    "スクロール領域",
    "AI の提案",
    "進行状況バー",
    "画像",
    "区切り線",
    "見出し",
    "チェックボックス",
    "領域",
    "エラー",
    "メニュー",
    "メニュー項目",
    "ダイアログ",
    "オプション",
    "セキュア",
    "空",
    "ビジー",
    "使用不可",
    "チェックあり",
    "チェックなし",
    "一部チェックあり",
    "選択済み",
    "淡色表示",
    "空白",
    "保留中",
    "承認済み",
    "却下",
    "AI の提案を承認しました",
    "AI の提案を却下しました",
    "スクリーンリーダー オン",
    "スクリーンリーダー オフ",
    "画面に何もありません",
    "末尾",
    "先頭",
    "次の見出しはありません",
    "前の見出しはありません",
    "次のランドマークはありません",
    "前のランドマークはありません",
    "次の AI の提案はありません",
    "前の AI の提案はありません",
    "フォーカスなし",
    "アクティブにできるものがありません",
    "コントロールではありません",
    "一致するアクションはありません",
    "Command Return で承認、または却下",
    "バナー ランドマーク",
    "ナビゲーション ランドマーク",
    "メイン ランドマーク",
    "検索 ランドマーク",
    "補足 ランドマーク",
    "ステータス ランドマーク",
    "詳細レベル 低",
    "詳細レベル 中",
    "詳細レベル 高",
    "エラー: ",
    "ショートカット",
    "{x} から",
    "{x} による",
    "約 {x}",
    "見出しレベル {n}",
    "{n} 件中 {k} 件目",
};
static const char *const C_ja[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, 0, 0, 0, 0, "{n} 項目"},
    {0, 0, 0, 0, 0, "{n} 文字"},
    {0, 0, 0, 0, 0, "{n} トークン"},
    {0, 0, 0, 0, 0, "{n} パーセント"},
};
static const char *const W_ko[ZUI_WORD_COUNT] = {
    "그룹",
    "버튼",
    "목록",
    "읽기 전용 텍스트",
    "텍스트 영역",
    "텍스트 필드",
    "스크롤 영역",
    "AI 제안",
    "진행률 표시줄",
    "이미지",
    "구분선",
    "제목",
    "체크박스",
    "영역",
    "오류",
    "메뉴",
    "메뉴 항목",
    "대화상자",
    "옵션",
    "보안",
    "비어 있음",
    "사용 중",
    "사용할 수 없음",
    "체크됨",
    "체크 안 됨",
    "일부 체크됨",
    "선택됨",
    "흐리게 표시됨",
    "공백",
    "대기 중",
    "수락됨",
    "거부됨",
    "AI 제안 수락됨",
    "AI 제안 거부됨",
    "스크린 리더 켜짐",
    "스크린 리더 꺼짐",
    "화면에 아무것도 없음",
    "끝",
    "맨 위",
    "다음 제목 없음",
    "이전 제목 없음",
    "다음 랜드마크 없음",
    "이전 랜드마크 없음",
    "다음 AI 제안 없음",
    "이전 AI 제안 없음",
    "포커스 없음",
    "활성화할 항목 없음",
    "컨트롤이 아님",
    "일치하는 동작 없음",
    "Command Return으로 수락하거나 거부",
    "배너 랜드마크",
    "탐색 랜드마크",
    "기본 랜드마크",
    "검색 랜드마크",
    "보충 랜드마크",
    "상태 랜드마크",
    "자세히 수준 낮음",
    "자세히 수준 보통",
    "자세히 수준 높음",
    "오류: ",
    "단축키",
    "{x}에서",
    "{x} 제공",
    "약 {x}",
    "제목 수준 {n}",
    "{n}개 중 {k}번째",
};
static const char *const C_ko[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, 0, 0, 0, 0, "{n}개 항목"},
    {0, 0, 0, 0, 0, "{n}자"},
    {0, 0, 0, 0, 0, "{n}개 토큰"},
    {0, 0, 0, 0, 0, "{n}퍼센트"},
};
static const char *const W_th[ZUI_WORD_COUNT] = {
    "กลุ่ม",
    "ปุ่ม",
    "รายการ",
    "ข้อความอ่านอย่างเดียว",
    "พื้นที่ข้อความ",
    "ช่องข้อความ",
    "พื้นที่เลื่อน",
    "คำแนะนำจาก AI",
    "แถบความคืบหน้า",
    "รูปภาพ",
    "ตัวคั่น",
    "หัวเรื่อง",
    "ช่องทำเครื่องหมาย",
    "ส่วน",
    "ข้อผิดพลาด",
    "เมนู",
    "รายการเมนู",
    "กล่องโต้ตอบ",
    "ตัวเลือก",
    "ปลอดภัย",
    "ว่าง",
    "ไม่ว่าง",
    "ปิดใช้งาน",
    "ทำเครื่องหมายแล้ว",
    "ไม่ได้ทำเครื่องหมาย",
    "ทำเครื่องหมายบางส่วน",
    "เลือกอยู่",
    "จาง",
    "ว่างเปล่า",
    "รอดำเนินการ",
    "ยอมรับแล้ว",
    "ปฏิเสธแล้ว",
    "ยอมรับคำแนะนำจาก AI แล้ว",
    "ปฏิเสธคำแนะนำจาก AI แล้ว",
    "เปิดโปรแกรมอ่านหน้าจอ",
    "ปิดโปรแกรมอ่านหน้าจอ",
    "ไม่มีอะไรบนหน้าจอ",
    "ท้ายสุด",
    "บนสุด",
    "ไม่มีหัวเรื่องถัดไป",
    "ไม่มีหัวเรื่องก่อนหน้า",
    "ไม่มีจุดสังเกตถัดไป",
    "ไม่มีจุดสังเกตก่อนหน้า",
    "ไม่มีคำแนะนำจาก AI ถัดไป",
    "ไม่มีคำแนะนำจาก AI ก่อนหน้า",
    "ไม่มีโฟกัส",
    "ไม่มีอะไรให้เปิดใช้งาน",
    "ไม่ใช่ตัวควบคุม",
    "ไม่มีการทำงานที่ตรงกัน",
    "ยอมรับด้วย Command Return หรือปฏิเสธ",
    "จุดสังเกตแบนเนอร์",
    "จุดสังเกตการนำทาง",
    "จุดสังเกตหลัก",
    "จุดสังเกตการค้นหา",
    "จุดสังเกตเสริม",
    "จุดสังเกตสถานะ",
    "ระดับรายละเอียด ต่ำ",
    "ระดับรายละเอียด กลาง",
    "ระดับรายละเอียด สูง",
    "ข้อผิดพลาด: ",
    "ทางลัด",
    "จาก {x}",
    "โดย {x}",
    "ประมาณ {x}",
    "หัวเรื่องระดับ {n}",
    "{k} จาก {n}",
};
static const char *const C_th[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, 0, 0, 0, 0, "{n} รายการ"},
    {0, 0, 0, 0, 0, "{n} อักขระ"},
    {0, 0, 0, 0, 0, "{n} โทเค็น"},
    {0, 0, 0, 0, 0, "{n} เปอร์เซ็นต์"},
};
static const char *const W_vi[ZUI_WORD_COUNT] = {
    "nhóm",
    "nút",
    "danh sách",
    "văn bản chỉ đọc",
    "vùng văn bản",
    "trường văn bản",
    "vùng cuộn",
    "gợi ý AI",
    "thanh tiến trình",
    "hình ảnh",
    "dấu phân cách",
    "tiêu đề",
    "hộp kiểm",
    "khu vực",
    "lỗi",
    "menu",
    "mục menu",
    "hộp thoại",
    "tùy chọn",
    "bảo mật",
    "trống",
    "đang bận",
    "không khả dụng",
    "đã đánh dấu",
    "chưa đánh dấu",
    "đánh dấu một phần",
    "đã chọn",
    "bị làm mờ",
    "trống",
    "đang chờ",
    "đã chấp nhận",
    "đã từ chối",
    "đã chấp nhận gợi ý AI",
    "đã từ chối gợi ý AI",
    "trình đọc màn hình bật",
    "trình đọc màn hình tắt",
    "không có gì trên màn hình",
    "cuối",
    "đầu",
    "không có tiêu đề tiếp theo",
    "không có tiêu đề trước đó",
    "không có mốc tiếp theo",
    "không có mốc trước đó",
    "không có gợi ý AI tiếp theo",
    "không có gợi ý AI trước đó",
    "không có tiêu điểm",
    "không có gì để kích hoạt",
    "không phải điều khiển",
    "không có hành động phù hợp",
    "chấp nhận bằng Command Return, hoặc từ chối",
    "mốc biểu ngữ",
    "mốc điều hướng",
    "mốc chính",
    "mốc tìm kiếm",
    "mốc bổ sung",
    "mốc trạng thái",
    "mức chi tiết thấp",
    "mức chi tiết trung bình",
    "mức chi tiết cao",
    "Lỗi: ",
    "phím tắt",
    "từ {x}",
    "bởi {x}",
    "khoảng {x}",
    "tiêu đề cấp {n}",
    "{k} trên {n}",
};
static const char *const C_vi[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, 0, 0, 0, 0, "{n} mục"},
    {0, 0, 0, 0, 0, "{n} ký tự"},
    {0, 0, 0, 0, 0, "{n} token"},
    {0, 0, 0, 0, 0, "{n} phần trăm"},
};
static const char *const W_tr[ZUI_WORD_COUNT] = {
    "grup",
    "düğme",
    "liste",
    "salt okunur metin",
    "çok satırlı metin alanı",
    "metin alanı",
    "kaydırma alanı",
    "yapay zekâ önerisi",
    "ilerleme çubuğu",
    "görüntü",
    "ayırıcı",
    "başlık",
    "onay kutusu",
    "bölge",
    "hata",
    "menü",
    "menü öğesi",
    "iletişim kutusu",
    "seçenek",
    "güvenli",
    "boş",
    "meşgul",
    "devre dışı",
    "işaretli",
    "işaretli değil",
    "kısmen işaretli",
    "seçili",
    "soluk",
    "boş",
    "beklemede",
    "kabul edildi",
    "reddedildi",
    "yapay zekâ önerisi kabul edildi",
    "yapay zekâ önerisi reddedildi",
    "ekran okuyucu açık",
    "ekran okuyucu kapalı",
    "ekranda hiçbir şey yok",
    "son",
    "baş",
    "sonraki başlık yok",
    "önceki başlık yok",
    "sonraki yer işareti yok",
    "önceki yer işareti yok",
    "sonraki yapay zekâ önerisi yok",
    "önceki yapay zekâ önerisi yok",
    "odak yok",
    "etkinleştirilecek bir şey yok",
    "denetim değil",
    "eşleşen eylem yok",
    "Command Return ile kabul edin veya reddedin",
    "başlık bandı yer işareti",
    "gezinme yer işareti",
    "ana yer işareti",
    "arama yer işareti",
    "tamamlayıcı yer işareti",
    "durum yer işareti",
    "ayrıntı düzeyi düşük",
    "ayrıntı düzeyi orta",
    "ayrıntı düzeyi yüksek",
    "Hata: ",
    "kısayol",
    "{x} tarafından",
    "{x} tarafından",
    "yaklaşık {x}",
    "başlık düzey {n}",
    "{k}/{n}",
};
static const char *const C_tr[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} öğe", 0, 0, 0, "{n} öğe"},
    {0, "{n} karakter", 0, 0, 0, "{n} karakter"},
    {0, "{n} token", 0, 0, 0, "{n} token"},
    {0, 0, 0, 0, 0, "yüzde {n}"},
};
static const char *const W_id[ZUI_WORD_COUNT] = {
    "grup",
    "tombol",
    "daftar",
    "teks hanya-baca",
    "area teks",
    "kolom teks",
    "area gulir",
    "saran AI",
    "bilah kemajuan",
    "gambar",
    "pemisah",
    "judul",
    "kotak centang",
    "wilayah",
    "kesalahan",
    "menu",
    "item menu",
    "dialog",
    "opsi",
    "aman",
    "kosong",
    "sibuk",
    "nonaktif",
    "dicentang",
    "tidak dicentang",
    "dicentang sebagian",
    "dipilih",
    "diredupkan",
    "kosong",
    "tertunda",
    "diterima",
    "ditolak",
    "saran AI diterima",
    "saran AI ditolak",
    "pembaca layar aktif",
    "pembaca layar nonaktif",
    "tidak ada apa pun di layar",
    "akhir",
    "awal",
    "tidak ada judul berikutnya",
    "tidak ada judul sebelumnya",
    "tidak ada penanda berikutnya",
    "tidak ada penanda sebelumnya",
    "tidak ada saran AI berikutnya",
    "tidak ada saran AI sebelumnya",
    "tidak ada fokus",
    "tidak ada yang bisa diaktifkan",
    "bukan kontrol",
    "tidak ada tindakan yang cocok",
    "terima dengan Command Return, atau tolak",
    "penanda spanduk",
    "penanda navigasi",
    "penanda utama",
    "penanda pencarian",
    "penanda pelengkap",
    "penanda status",
    "tingkat detail rendah",
    "tingkat detail sedang",
    "tingkat detail tinggi",
    "Kesalahan: ",
    "pintasan",
    "dari {x}",
    "oleh {x}",
    "sekitar {x}",
    "judul tingkat {n}",
    "{k} dari {n}",
};
static const char *const C_id[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, 0, 0, 0, 0, "{n} item"},
    {0, 0, 0, 0, 0, "{n} karakter"},
    {0, 0, 0, 0, 0, "{n} token"},
    {0, 0, 0, 0, 0, "{n} persen"},
};
static const char *const W_sw[ZUI_WORD_COUNT] = {
    "kikundi",
    "kitufe",
    "orodha",
    "maandishi ya kusoma tu",
    "eneo la maandishi",
    "sehemu ya maandishi",
    "eneo la kusogeza",
    "pendekezo la AI",
    "upau wa maendeleo",
    "picha",
    "kitenganishi",
    "kichwa",
    "kisanduku cha kuteua",
    "eneo",
    "hitilafu",
    "menyu",
    "kipengee cha menyu",
    "kidirisha",
    "chaguo",
    "salama",
    "tupu",
    "inashughulika",
    "imezimwa",
    "imeteuliwa",
    "haijateuliwa",
    "imeteuliwa kiasi",
    "imechaguliwa",
    "imefifishwa",
    "tupu",
    "inasubiri",
    "imekubaliwa",
    "imekataliwa",
    "pendekezo la AI limekubaliwa",
    "pendekezo la AI limekataliwa",
    "kisoma skrini kimewashwa",
    "kisoma skrini kimezimwa",
    "hakuna kitu kwenye skrini",
    "mwisho",
    "mwanzo",
    "hakuna kichwa kinachofuata",
    "hakuna kichwa kilichotangulia",
    "hakuna alama inayofuata",
    "hakuna alama iliyotangulia",
    "hakuna pendekezo la AI linalofuata",
    "hakuna pendekezo la AI lililotangulia",
    "hakuna kilicholengwa",
    "hakuna cha kuwasha",
    "si kidhibiti",
    "hakuna vitendo vinavyolingana",
    "kubali kwa Command Return, au kataa",
    "alama ya bango",
    "alama ya usogezaji",
    "alama kuu",
    "alama ya utafutaji",
    "alama ya ziada",
    "alama ya hali",
    "kiwango cha maelezo cha chini",
    "kiwango cha maelezo cha wastani",
    "kiwango cha maelezo cha juu",
    "Hitilafu: ",
    "njia ya mkato",
    "kutoka kwa {x}",
    "na {x}",
    "takriban {x}",
    "kichwa cha kiwango cha {n}",
    "{k} kati ya {n}",
};
static const char *const C_sw[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "kipengee {n}", 0, 0, 0, "vipengee {n}"},
    {0, "herufi {n}", 0, 0, 0, "herufi {n}"},
    {0, "tokeni {n}", 0, 0, 0, "tokeni {n}"},
    {0, 0, 0, 0, 0, "asilimia {n}"},
};
static const char *const W_am[ZUI_WORD_COUNT] = {
    "ቡድን",
    "አዝራር",
    "ዝርዝር",
    "ለንባብ ብቻ የሆነ ጽሑፍ",
    "የጽሑፍ ቦታ",
    "የጽሑፍ መስክ",
    "የማሸብለያ ቦታ",
    "የAI ጥቆማ",
    "የሂደት አሞሌ",
    "ምስል",
    "መለያያ",
    "ርዕስ",
    "አመልካች ሳጥን",
    "ክልል",
    "ስህተት",
    "ምናሌ",
    "የምናሌ ንጥል",
    "የንግግር ሳጥን",
    "አማራጭ",
    "ደህንነቱ የተጠበቀ",
    "ባዶ",
    "ሥራ ላይ",
    "ተሰናክሏል",
    "ምልክት ተደርጓል",
    "ምልክት አልተደረገም",
    "በከፊል ምልክት ተደርጓል",
    "ተመርጧል",
    "ደብዝዟል",
    "ባዶ",
    "በመጠባበቅ ላይ",
    "ተቀባይነት አግኝቷል",
    "ውድቅ ተደርጓል",
    "የAI ጥቆማ ተቀባይነት አግኝቷል",
    "የAI ጥቆማ ውድቅ ተደርጓል",
    "የማያ ገጽ አንባቢ በርቷል",
    "የማያ ገጽ አንባቢ ጠፍቷል",
    "በማያ ገጹ ላይ ምንም የለም",
    "መጨረሻ",
    "መጀመሪያ",
    "ቀጣይ ርዕስ የለም",
    "ቀዳሚ ርዕስ የለም",
    "ቀጣይ ምልክት የለም",
    "ቀዳሚ ምልክት የለም",
    "ቀጣይ የAI ጥቆማ የለም",
    "ቀዳሚ የAI ጥቆማ የለም",
    "ትኩረት የለም",
    "የሚነቃ ነገር የለም",
    "መቆጣጠሪያ አይደለም",
    "የሚዛመድ እርምጃ የለም",
    "በCommand Return ይቀበሉ፣ ወይም ውድቅ ያድርጉ",
    "የሰንደቅ ምልክት",
    "የአሰሳ ምልክት",
    "ዋና ምልክት",
    "የፍለጋ ምልክት",
    "ተጨማሪ ምልክት",
    "የሁኔታ ምልክት",
    "የዝርዝር ደረጃ ዝቅተኛ",
    "የዝርዝር ደረጃ መካከለኛ",
    "የዝርዝር ደረጃ ከፍተኛ",
    "ስህተት: ",
    "አቋራጭ",
    "ከ{x}",
    "በ{x}",
    "በግምት {x}",
    "ርዕስ ደረጃ {n}",
    "{k} ከ{n}",
};
static const char *const C_am[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} ንጥል", 0, 0, 0, "{n} ንጥሎች"},
    {0, "{n} ቁምፊ", 0, 0, 0, "{n} ቁምፊዎች"},
    {0, "{n} ቶከን", 0, 0, 0, "{n} ቶከኖች"},
    {0, 0, 0, 0, 0, "{n} በመቶ"},
};
static const char *const W_yo[ZUI_WORD_COUNT] = {
    "ẹgbẹ́",
    "bọ́tìnnì",
    "àkójọ",
    "ọ̀rọ̀ fún kíkà nìkan",
    "agbègbè ọ̀rọ̀",
    "àyè ọ̀rọ̀",
    "agbègbè yíyí",
    "àbá AI",
    "ọ̀pá ìlọsíwájú",
    "àwòrán",
    "ìpínyà",
    "àkọlé",
    "àpótí àyẹ̀wò",
    "agbègbè",
    "àṣìṣe",
    "àkójọ àṣàyàn",
    "ohun àkójọ àṣàyàn",
    "àpótí ìbánisọ̀rọ̀",
    "àṣàyàn",
    "ààbò",
    "ṣófo",
    "ọwọ́ dí",
    "a ti pa á",
    "a ti fi àmì sí",
    "a kò fi àmì sí",
    "a fi àmì sí díẹ̀",
    "a ti yàn án",
    "ó ti dín",
    "òfìfo",
    "ó ń dúró",
    "a ti gbà á",
    "a ti kọ̀ ọ́",
    "a ti gba àbá AI",
    "a ti kọ àbá AI",
    "olùka ojú-ìbojú wà ní títàn",
    "olùka ojú-ìbojú ti wà ní pípa",
    "kò sí nǹkan lójú-ìbojú",
    "òpin",
    "òkè",
    "kò sí àkọlé tó kàn",
    "kò sí àkọlé ìṣáájú",
    "kò sí àmì-ilẹ̀ tó kàn",
    "kò sí àmì-ilẹ̀ ìṣáájú",
    "kò sí àbá AI tó kàn",
    "kò sí àbá AI ìṣáájú",
    "kò sí àfojúsùn",
    "kò sí nǹkan láti mú ṣiṣẹ́",
    "kì í ṣe ìdarí",
    "kò sí ìgbésẹ̀ tó bá a mu",
    "gbà á pẹ̀lú Command Return, tàbí kọ̀ ọ́",
    "àmì-ilẹ̀ àsíá",
    "àmì-ilẹ̀ ìrìnkiri",
    "àmì-ilẹ̀ pàtàkì",
    "àmì-ilẹ̀ ìwákiri",
    "àmì-ilẹ̀ àfikún",
    "àmì-ilẹ̀ ipò",
    "ìpele àlàyé kékeré",
    "ìpele àlàyé àárín",
    "ìpele àlàyé gíga",
    "Àṣìṣe: ",
    "ọ̀nà àbùjá",
    "láti ọ̀dọ̀ {x}",
    "láti ọwọ́ {x}",
    "nǹkan bí {x}",
    "àkọlé ìpele {n}",
    "{k} nínú {n}",
};
static const char *const C_yo[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, 0, 0, 0, 0, "ohun {n}"},
    {0, 0, 0, 0, 0, "lẹ́tà {n}"},
    {0, 0, 0, 0, 0, "tókìnnì {n}"},
    {0, 0, 0, 0, 0, "ìdá {n} nínú ọgọ́rùn-ún"},
};
static const char *const W_ha[ZUI_WORD_COUNT] = {
    "rukuni",
    "maɓalli",
    "jeri",
    "rubutu na karantawa kawai",
    "yankin rubutu",
    "filin rubutu",
    "yankin gungurawa",
    "shawarar AI",
    "sandar ci gaba",
    "hoto",
    "mai raba",
    "kanu",
    "akwatin zaɓi",
    "yanki",
    "kuskure",
    "menu",
    "abun menu",
    "akwatin tattaunawa",
    "zaɓi",
    "amintacce",
    "fanko",
    "yana aiki",
    "an kashe",
    "an duba",
    "ba a duba ba",
    "an duba wani ɓangare",
    "an zaɓa",
    "an dusashe",
    "babu komai",
    "ana jira",
    "an karɓa",
    "an ƙi",
    "an karɓi shawarar AI",
    "an ƙi shawarar AI",
    "mai karanta allo yana kunne",
    "mai karanta allo a kashe",
    "babu komai a kan allo",
    "ƙarshe",
    "farko",
    "babu kanu na gaba",
    "babu kanu na baya",
    "babu alamar ƙasa ta gaba",
    "babu alamar ƙasa ta baya",
    "babu shawarar AI ta gaba",
    "babu shawarar AI ta baya",
    "babu mayar da hankali",
    "babu abin da za a kunna",
    "ba sarrafawa ba ne",
    "babu ayyukan da suka dace",
    "karɓa da Command Return, ko ƙi",
    "alamar ƙasa ta tuta",
    "alamar ƙasa ta kewayawa",
    "babbar alamar ƙasa",
    "alamar ƙasa ta bincike",
    "alamar ƙasa ta ƙari",
    "alamar ƙasa ta matsayi",
    "matakin bayani ƙasa",
    "matakin bayani matsakaici",
    "matakin bayani sama",
    "Kuskure: ",
    "gajeriyar hanya",
    "daga {x}",
    "ta {x}",
    "kusan {x}",
    "kanu mataki na {n}",
    "{k} cikin {n}",
};
static const char *const C_ha[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "abu {n}", 0, 0, 0, "abubuwa {n}"},
    {0, "harafi {n}", 0, 0, 0, "haruffa {n}"},
    {0, "alama {n}", 0, 0, 0, "alamomi {n}"},
    {0, 0, 0, 0, 0, "kashi {n} cikin ɗari"},
};
static const char *const W_el[ZUI_WORD_COUNT] = {
    "ομάδα",
    "κουμπί",
    "λίστα",
    "κείμενο μόνο για ανάγνωση",
    "περιοχή κειμένου",
    "πεδίο κειμένου",
    "περιοχή κύλισης",
    "πρόταση ΤΝ",
    "γραμμή προόδου",
    "εικόνα",
    "διαχωριστικό",
    "επικεφαλίδα",
    "πλαίσιο ελέγχου",
    "περιοχή",
    "σφάλμα",
    "μενού",
    "στοιχείο μενού",
    "παράθυρο διαλόγου",
    "επιλογή",
    "προστατευμένο",
    "κενό",
    "απασχολημένο",
    "μη διαθέσιμο",
    "τσεκαρισμένο",
    "μη τσεκαρισμένο",
    "μερικώς τσεκαρισμένο",
    "επιλεγμένο",
    "αχνό",
    "κενό",
    "σε αναμονή",
    "αποδεκτή",
    "απορρίφθηκε",
    "η πρόταση ΤΝ έγινε αποδεκτή",
    "η πρόταση ΤΝ απορρίφθηκε",
    "αναγνώστης οθόνης ενεργός",
    "αναγνώστης οθόνης ανενεργός",
    "τίποτα στην οθόνη",
    "τέλος",
    "αρχή",
    "καμία επόμενη επικεφαλίδα",
    "καμία προηγούμενη επικεφαλίδα",
    "κανένα επόμενο ορόσημο",
    "κανένα προηγούμενο ορόσημο",
    "καμία επόμενη πρόταση ΤΝ",
    "καμία προηγούμενη πρόταση ΤΝ",
    "χωρίς εστίαση",
    "τίποτα για ενεργοποίηση",
    "δεν είναι στοιχείο ελέγχου",
    "καμία ενέργεια δεν ταιριάζει",
    "αποδοχή με Command Return ή απόρριψη",
    "ορόσημο κεφαλίδας",
    "ορόσημο πλοήγησης",
    "κύριο ορόσημο",
    "ορόσημο αναζήτησης",
    "συμπληρωματικό ορόσημο",
    "ορόσημο κατάστασης",
    "λεπτομέρεια χαμηλή",
    "λεπτομέρεια μεσαία",
    "λεπτομέρεια υψηλή",
    "Σφάλμα: ",
    "συντόμευση",
    "από {x}",
    "από {x}",
    "περίπου {x}",
    "επικεφαλίδα επιπέδου {n}",
    "{k} από {n}",
};
static const char *const C_el[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} στοιχείο", 0, 0, 0, "{n} στοιχεία"},
    {0, "{n} χαρακτήρας", 0, 0, 0, "{n} χαρακτήρες"},
    {0, "{n} διακριτικό", 0, 0, 0, "{n} διακριτικά"},
    {0, 0, 0, 0, 0, "{n} τοις εκατό"},
};
static const char *const W_nl[ZUI_WORD_COUNT] = {
    "groep",
    "knop",
    "lijst",
    "alleen-lezentekst",
    "tekstgebied",
    "tekstveld",
    "scrollgebied",
    "AI-suggestie",
    "voortgangsbalk",
    "afbeelding",
    "scheidingslijn",
    "kop",
    "selectievakje",
    "regio",
    "fout",
    "menu",
    "menu-onderdeel",
    "dialoogvenster",
    "optie",
    "beveiligd",
    "leeg",
    "bezig",
    "uitgeschakeld",
    "aangevinkt",
    "niet aangevinkt",
    "gedeeltelijk aangevinkt",
    "geselecteerd",
    "gedimd",
    "leeg",
    "in afwachting",
    "geaccepteerd",
    "afgewezen",
    "AI-suggestie geaccepteerd",
    "AI-suggestie afgewezen",
    "schermlezer aan",
    "schermlezer uit",
    "niets op het scherm",
    "einde",
    "begin",
    "geen volgende kop",
    "geen vorige kop",
    "geen volgend oriëntatiepunt",
    "geen vorig oriëntatiepunt",
    "geen volgende AI-suggestie",
    "geen vorige AI-suggestie",
    "geen focus",
    "niets om te activeren",
    "geen bedieningselement",
    "geen overeenkomende acties",
    "accepteren met Command Return, of afwijzen",
    "oriëntatiepunt banner",
    "oriëntatiepunt navigatie",
    "hoofdoriëntatiepunt",
    "oriëntatiepunt zoeken",
    "aanvullend oriëntatiepunt",
    "oriëntatiepunt status",
    "detailniveau laag",
    "detailniveau gemiddeld",
    "detailniveau hoog",
    "Fout: ",
    "sneltoets",
    "van {x}",
    "door {x}",
    "ongeveer {x}",
    "kop niveau {n}",
    "{k} van {n}",
};
static const char *const C_nl[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {
    {0, "{n} item", 0, 0, 0, "{n} items"},
    {0, "{n} teken", 0, 0, 0, "{n} tekens"},
    {0, "{n} token", 0, 0, 0, "{n} tokens"},
    {0, 0, 0, 0, 0, "{n} procent"},
};

static const struct zui_locale LOCALES[30] = {
    {"en", "English", 0, ZUI_PLURAL_RULE_ONE, 0, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_LATN, ".", ",", "-"}},
     "M/d/yy", "h:mm\u202Fa", "AM", "PM", ", ", W_en, C_en}, /* English, region US */
    {"es", "español", 0, ZUI_PLURAL_RULE_ONE_MILLION, 1, CAL_GREGORIAN, 3, 3, 2,
     {{SYS_LATN, ",", ".", "-"}, {SYS_LATN, ",", ".", "-"}},
     "d/M/yy", "H:mm", "a.\u202Fm.", "p.\u202Fm.", ", ", W_es, C_es}, /* español, region ES */
    {"fr", "français", 0, ZUI_PLURAL_RULE_ZERO_ONE_MILLION, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", "\u202F", "-"}, {SYS_LATN, ",", "\u202F", "-"}},
     "dd/MM/y", "HH:mm", "AM", "PM", ", ", W_fr, C_fr}, /* français, region FR */
    {"de", "Deutsch", 0, ZUI_PLURAL_RULE_ONE, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", ".", "-"}, {SYS_LATN, ",", ".", "-"}},
     "dd.MM.yy", "HH:mm", "AM", "PM", ", ", W_de, C_de}, /* Deutsch, region DE */
    {"pt", "português", 0, ZUI_PLURAL_RULE_ZERO_ONE_MILLION, 0, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", ".", "-"}, {SYS_LATN, ",", ".", "-"}},
     "dd/MM/y", "HH:mm", "AM", "PM", ", ", W_pt, C_pt}, /* português, region BR */
    {"it", "italiano", 0, ZUI_PLURAL_RULE_ONE_MILLION, 1, CAL_GREGORIAN, 3, 3, 2,
     {{SYS_LATN, ",", ".", "-"}, {SYS_LATN, ",", ".", "-"}},
     "dd/MM/yy", "HH:mm", "AM", "PM", ", ", W_it, C_it}, /* italiano, region IT */
    {"ru", "русский", 0, ZUI_PLURAL_RULE_SLAVIC, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", "\u00A0", "-"}, {SYS_LATN, ",", "\u00A0", "-"}},
     "dd.MM.y", "HH:mm", "AM", "PM", ", ", W_ru, C_ru}, /* русский, region RU */
    {"uk", "українська", 0, ZUI_PLURAL_RULE_SLAVIC, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", "\u00A0", "-"}, {SYS_LATN, ",", "\u00A0", "-"}},
     "dd.MM.yy", "HH:mm", "дп", "пп", ", ", W_uk, C_uk}, /* українська, region UA */
    {"pl", "polski", 0, ZUI_PLURAL_RULE_POLISH, 1, CAL_GREGORIAN, 3, 3, 2,
     {{SYS_LATN, ",", "\u00A0", "-"}, {SYS_LATN, ",", "\u00A0", "-"}},
     "d.MM.y", "HH:mm", "AM", "PM", ", ", W_pl, C_pl}, /* polski, region PL */
    {"ar", "العربية", 1, ZUI_PLURAL_RULE_ARABIC, 6, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "\u200E-"}, {SYS_ARAB, "٫", "٬", "\u061C-"}},
     "d\u200F/M\u200F/y", "h:mm a", "ص", "م", "، ", W_ar, C_ar}, /* العربية, region EG */
    {"he", "עברית", 1, ZUI_PLURAL_RULE_HEBREW, 0, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "\u200E-"}, {SYS_LATN, ".", ",", "\u200E-"}},
     "d.M.y", "H:mm", "AM", "PM", ", ", W_he, C_he}, /* עברית, region IL */
    {"fa", "فارسی", 1, ZUI_PLURAL_RULE_ZERO_ONE, 6, CAL_PERSIAN, 3, 3, 1,
     {{SYS_ARABEXT, "٫", "٬", "\u200E\u2212"}, {SYS_ARABEXT, "٫", "٬", "\u200E\u2212"}},
     "y/M/d", "H:mm", "ق.ظ.", "ب.ظ.", "، ", W_fa, C_fa}, /* فارسی, region IR */
    {"ur", "اردو", 1, ZUI_PLURAL_RULE_ONE, 0, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "\u200E-"}, {SYS_ARABEXT, "٫", "٬", "\u200E-\u200E"}},
     "d/M/yy", "h:mm a", "AM", "PM", "، ", W_ur, C_ur}, /* اردو, region PK */
    {"hi", "हिन्दी", 0, ZUI_PLURAL_RULE_ZERO_ONE, 0, CAL_GREGORIAN, 3, 2, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_DEVA, ".", ",", "-"}},
     "d/M/yy", "h:mm a", "am", "pm", ", ", W_hi, C_hi}, /* हिन्दी, region IN */
    {"bn", "বাংলা", 0, ZUI_PLURAL_RULE_ZERO_ONE, 0, CAL_GREGORIAN, 3, 2, 1,
     {{SYS_BENG, ".", ",", "-"}, {SYS_BENG, ".", ",", "-"}},
     "d/M/yy", "h:mm a", "AM", "PM", ", ", W_bn, C_bn}, /* বাংলা, region BD */
    {"ta", "தமிழ்", 0, ZUI_PLURAL_RULE_ONE, 0, CAL_GREGORIAN, 3, 2, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_TAMLDEC, ".", ",", "-"}},
     "d/M/yy", "h:mm a", "AM", "PM", ", ", W_ta, C_ta}, /* தமிழ், region IN */
    {"zh-Hans", "简体中文", 0, ZUI_PLURAL_RULE_OTHER, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_HANIDEC, ".", ",", "-"}},
     "y/M/d", "HH:mm", "上午", "下午", "，", W_zh_Hans, C_zh_Hans}, /* 简体中文, region CN */
    {"zh-Hant", "繁體中文", 0, ZUI_PLURAL_RULE_OTHER, 0, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_HANIDEC, ".", ",", "-"}},
     "y/M/d", "Bh:mm", "上午", "下午", "，", W_zh_Hant, C_zh_Hant}, /* 繁體中文, region TW */
    {"ja", "日本語", 0, ZUI_PLURAL_RULE_OTHER, 0, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_LATN, ".", ",", "-"}},
     "y/MM/dd", "H:mm", "午前", "午後", "、", W_ja, C_ja}, /* 日本語, region JP */
    {"ko", "한국어", 0, ZUI_PLURAL_RULE_OTHER, 0, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_LATN, ".", ",", "-"}},
     "yy. M. d.", "a h:mm", "오전", "오후", ", ", W_ko, C_ko}, /* 한국어, region KR */
    {"th", "ไทย", 0, ZUI_PLURAL_RULE_OTHER, 0, CAL_BUDDHIST, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_THAI, ".", ",", "-"}},
     "d/M/yy", "HH:mm", "AM", "PM", " ", W_th, C_th}, /* ไทย, region TH */
    {"vi", "Tiếng Việt", 0, ZUI_PLURAL_RULE_OTHER, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", ".", "-"}, {SYS_LATN, ",", ".", "-"}},
     "d/M/yy", "HH:mm", "SA", "CH", ", ", W_vi, C_vi}, /* Tiếng Việt, region VN */
    {"tr", "Türkçe", 0, ZUI_PLURAL_RULE_ONE, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", ".", "-"}, {SYS_LATN, ",", ".", "-"}},
     "d.MM.y", "HH:mm", "ÖÖ", "ÖS", ", ", W_tr, C_tr}, /* Türkçe, region TR */
    {"id", "Bahasa Indonesia", 0, ZUI_PLURAL_RULE_OTHER, 0, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", ".", "-"}, {SYS_LATN, ",", ".", "-"}},
     "dd/MM/yy", "HH.mm", "AM", "PM", ", ", W_id, C_id}, /* Bahasa Indonesia, region ID */
    {"sw", "Kiswahili", 0, ZUI_PLURAL_RULE_ONE, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_LATN, ".", ",", "-"}},
     "dd/MM/y", "HH:mm", "AM", "PM", ", ", W_sw, C_sw}, /* Kiswahili, region TZ */
    {"am", "አማርኛ", 0, ZUI_PLURAL_RULE_ZERO_ONE, 0, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_LATN, ".", ",", "-"}},
     "dd/MM/y", "h:mm a", "ጥዋት", "ከሰዓት", "፣ ", W_am, C_am}, /* አማርኛ, region ET */
    {"yo", "Èdè Yorùbá", 0, ZUI_PLURAL_RULE_OTHER, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_LATN, ".", ",", "-"}},
     "d/M/y", "H:m", "Àárọ̀", "Ọ̀sán", ", ", W_yo, C_yo}, /* Èdè Yorùbá, region NG */
    {"ha", "Hausa", 0, ZUI_PLURAL_RULE_ONE, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ".", ",", "-"}, {SYS_LATN, ".", ",", "-"}},
     "d/M/yy", "HH:mm", "SF", "YM", ", ", W_ha, C_ha}, /* Hausa, region NG */
    {"el", "Ελληνικά", 0, ZUI_PLURAL_RULE_ONE, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", ".", "-"}, {SYS_LATN, ",", ".", "-"}},
     "d/M/yy", "h:mm\u202Fa", "π.μ.", "μ.μ.", ", ", W_el, C_el}, /* Ελληνικά, region GR */
    {"nl", "Nederlands", 0, ZUI_PLURAL_RULE_ONE, 1, CAL_GREGORIAN, 3, 3, 1,
     {{SYS_LATN, ",", ".", "-"}, {SYS_LATN, ",", ".", "-"}},
     "dd-MM-y", "HH:mm", "a.m.", "p.m.", ", ", W_nl, C_nl}, /* Nederlands, region NL */
};
// clang-format on

#define EN (&LOCALES[0])

/* ---- small helpers --------------------------------------------------------- */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static bool seq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

/* The longest prefix of s[0..n) that is whole UTF-8 characters and fits. */
static uint32_t utf8_fit(const char *s, uint32_t n, uint32_t room)
{
    if (n <= room) return n;
    n = room;
    while (n > 0 && ((uint8_t) s[n] & 0xC0u) == 0x80u) n--;
    return n;
}

typedef struct {
    char *out;
    uint32_t cap, n;
    bool full;
} sb_t;

static void sb_n(sb_t *b, const char *s, uint32_t n)
{
    if (!b->cap || !s || b->full) return;
    uint32_t room = b->cap - 1u - b->n;
    if (n > room) {
        n = utf8_fit(s, n, room);
        b->full = true;
    }
    for (uint32_t i = 0; i < n; i++) b->out[b->n++] = s[i];
    b->out[b->n] = 0;
}

static void sb_s(sb_t *b, const char *s)
{
    sb_n(b, s, slen(s));
}

static void sb_cp(sb_t *b, uint32_t cp)
{
    char u[4];
    uint32_t k = zui_utf8_encode(cp, u, sizeof u);
    sb_n(b, u, k);
}

static sb_t sb_start(char *out, uint32_t cap)
{
    sb_t b = {out, cap, 0, false};
    if (cap && out) out[0] = 0;
    if (!out) b.cap = 0;
    return b;
}

/* ---- L1: finding a locale ---------------------------------------------------- */

uint32_t zui_locale_count_all(void)
{
    return (uint32_t) (sizeof LOCALES / sizeof LOCALES[0]);
}

const zui_locale_t *zui_locale_at(uint32_t i)
{
    return i < zui_locale_count_all() ? &LOCALES[i] : 0;
}

static const zui_locale_t *by_tag(const char *t)
{
    for (uint32_t i = 0; i < zui_locale_count_all(); i++)
        if (seq(LOCALES[i].tag, t)) return &LOCALES[i];
    return 0;
}

const zui_locale_t *zui_locale_find(const char *tag)
{
    char lang[9] = {0}, script[5] = {0}, region[4] = {0};
    uint32_t i = 0, k = 0;
    if (!tag) return EN;
    while (tag[i] && tag[i] != '-' && tag[i] != '_' && k < 8) lang[k++] = lower(tag[i++]);
    if (tag[i] && tag[i] != '-' && tag[i] != '_') return EN; /* a language longer than 8 */
    while (tag[i] == '-' || tag[i] == '_') {
        i++;
        uint32_t s = i;
        while (tag[i] && tag[i] != '-' && tag[i] != '_') i++;
        uint32_t len = i - s;
        if (len == 4 && !script[0] && !region[0]) {
            script[0] = (char) (lower(tag[s]) - 'a' + 'A');
            for (uint32_t j = 1; j < 4; j++) script[j] = lower(tag[s + j]);
        } else if ((len == 2 || len == 3) && !region[0]) {
            for (uint32_t j = 0; j < len; j++) {
                char c = tag[s + j];
                region[j] = (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c;
            }
        }
    }
    if (seq(lang, "iw")) return by_tag("he");
    if (seq(lang, "in")) return by_tag("id");
    if (seq(lang, "zh")) {
        if (seq(script, "Hant")) return by_tag("zh-Hant");
        if (seq(script, "Hans")) return by_tag("zh-Hans");
        if (seq(region, "TW") || seq(region, "HK") || seq(region, "MO")) return by_tag("zh-Hant");
        return by_tag("zh-Hans");
    }
    const zui_locale_t *l = by_tag(lang);
    return l ? l : EN;
}

static const zui_locale_t *L_(const zui_locale_t *loc)
{
    return loc ? loc : EN;
}

const char *zui_locale_tag(const zui_locale_t *loc)
{
    return L_(loc)->tag;
}
const char *zui_locale_name(const zui_locale_t *loc)
{
    return L_(loc)->name;
}
bool zui_locale_rtl(const zui_locale_t *loc)
{
    return L_(loc)->rtl != 0;
}
uint8_t zui_locale_first_day(const zui_locale_t *loc)
{
    return L_(loc)->first_day;
}
const char *zui_locale_list_separator(const zui_locale_t *loc)
{
    return L_(loc)->list;
}

/* ---- L3: plurals --------------------------------------------------------------- */

const char *zui_plural_name(zui_plural_t p)
{
    static const char *const N[ZUI_PLURAL_COUNT] = {"zero", "one", "two", "few", "many", "other"};
    return (uint32_t) p < ZUI_PLURAL_COUNT ? N[p] : "other";
}

zui_plural_t zui_locale_plural(const zui_locale_t *loc, int64_t n)
{
    uint64_t i = n < 0 ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
    uint64_t m10 = i % 10u, m100 = i % 100u;
    switch (L_(loc)->plural) {
    case ZUI_PLURAL_RULE_ONE:
        return i == 1 ? ZUI_PLURAL_ONE : ZUI_PLURAL_OTHER;
    case ZUI_PLURAL_RULE_ONE_MILLION:
        if (i == 1) return ZUI_PLURAL_ONE;
        return (i != 0 && i % 1000000u == 0) ? ZUI_PLURAL_MANY : ZUI_PLURAL_OTHER;
    case ZUI_PLURAL_RULE_ZERO_ONE_MILLION:
        if (i <= 1) return ZUI_PLURAL_ONE;
        return (i % 1000000u == 0) ? ZUI_PLURAL_MANY : ZUI_PLURAL_OTHER;
    case ZUI_PLURAL_RULE_ZERO_ONE:
        return i <= 1 ? ZUI_PLURAL_ONE : ZUI_PLURAL_OTHER;
    case ZUI_PLURAL_RULE_SLAVIC:
        if (m10 == 1 && m100 != 11) return ZUI_PLURAL_ONE;
        if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return ZUI_PLURAL_FEW;
        return ZUI_PLURAL_MANY;
    case ZUI_PLURAL_RULE_POLISH:
        if (i == 1) return ZUI_PLURAL_ONE;
        if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return ZUI_PLURAL_FEW;
        return ZUI_PLURAL_MANY;
    case ZUI_PLURAL_RULE_ARABIC:
        if (i == 0) return ZUI_PLURAL_ZERO;
        if (i == 1) return ZUI_PLURAL_ONE;
        if (i == 2) return ZUI_PLURAL_TWO;
        if (m100 >= 3 && m100 <= 10) return ZUI_PLURAL_FEW;
        if (m100 >= 11) return ZUI_PLURAL_MANY;
        return ZUI_PLURAL_OTHER;
    case ZUI_PLURAL_RULE_HEBREW:
        if (i == 1) return ZUI_PLURAL_ONE;
        if (i == 2) return ZUI_PLURAL_TWO;
        return ZUI_PLURAL_OTHER;
    default:
        return ZUI_PLURAL_OTHER;
    }
}

/* ---- L4: numbers ----------------------------------------------------------------- */

static const numsys_t *numsys(const zui_locale_t *l, zui_digits_t d)
{
    static const numsys_t LATIN = {SYS_LATN, ".", ",", "-"};
    if (d == ZUI_DIGITS_NATIVE) return &l->num[1];
    if (d == ZUI_DIGITS_LATIN) {
        /* Latin digits keep the locale's own symbols when its default is Latin */
        return l->num[0].sys == SYS_LATN ? &l->num[0] : &LATIN;
    }
    return &l->num[0];
}

static uint32_t digit_cp(uint8_t sys, uint32_t d)
{
    static const uint16_t HANI[10] = {0x3007, 0x4E00, 0x4E8C, 0x4E09, 0x56DB,
                                      0x4E94, 0x516D, 0x4E03, 0x516B, 0x4E5D};
    switch (sys) {
    case SYS_ARAB:
        return 0x0660u + d;
    case SYS_ARABEXT:
        return 0x06F0u + d;
    case SYS_BENG:
        return 0x09E6u + d;
    case SYS_DEVA:
        return 0x0966u + d;
    case SYS_THAI:
        return 0x0E50u + d;
    case SYS_TAMLDEC:
        return 0x0BE6u + d;
    case SYS_HANIDEC:
        return HANI[d];
    default:
        return '0' + d;
    }
}

uint32_t zui_locale_zero(const zui_locale_t *loc, zui_digits_t digits)
{
    return digit_cp(numsys(L_(loc), digits)->sys, 0);
}

static void put_number(sb_t *b, const zui_locale_t *l, int64_t value, uint32_t scale,
                       zui_digits_t digits)
{
    const numsys_t *ns = numsys(l, digits);
    uint64_t v = value < 0 ? (uint64_t) 0 - (uint64_t) value : (uint64_t) value;
    char d[24];
    uint32_t n = 0;
    if (scale > 18) scale = 18;
    do {
        d[n++] = (char) (v % 10u);
        v /= 10u;
    } while (v && n < sizeof d);
    while (n <= scale) d[n++] = 0; /* "0.05" */
    /* d[] holds the digits least significant first */
    uint32_t ints = n - scale;
    if (value < 0) sb_s(b, ns->minus);
    bool group = l->g1 && ints >= (uint32_t) l->g1 + l->min_group;
    for (uint32_t k = ints; k-- > 0;) {
        sb_cp(b, digit_cp(ns->sys, (uint32_t) d[scale + k]));
        if (group && k > 0) {
            /* a separator after this digit when k digits remain */
            bool sep = k == l->g1 || (k > l->g1 && (k - l->g1) % l->g2 == 0);
            if (sep) sb_s(b, ns->group);
        }
    }
    if (scale) {
        sb_s(b, ns->decimal);
        for (uint32_t k = scale; k-- > 0;) sb_cp(b, digit_cp(ns->sys, (uint32_t) d[k]));
    }
}

uint32_t zui_locale_number(const zui_locale_t *loc, int64_t value, uint32_t scale,
                           zui_digits_t digits, char *out, uint32_t cap)
{
    sb_t b = sb_start(out, cap);
    put_number(&b, L_(loc), value, scale, digits);
    return b.n;
}

uint32_t zui_locale_integer(const zui_locale_t *loc, int64_t value, char *out, uint32_t cap)
{
    return zui_locale_number(loc, value, 0, ZUI_DIGITS_DEFAULT, out, cap);
}

/* digits without grouping, zero-padded to width (dates and times) */
static void put_field(sb_t *b, uint8_t sys, uint64_t v, uint32_t width)
{
    char d[24];
    uint32_t n = 0;
    do {
        d[n++] = (char) (v % 10u);
        v /= 10u;
    } while (v && n < sizeof d);
    while (n < width && n < sizeof d) d[n++] = 0;
    while (n-- > 0) sb_cp(b, digit_cp(sys, (uint32_t) d[n]));
}

/* ---- L5: dates and times ------------------------------------------------------------ */

/* days since 1970-01-01 of a Gregorian date, and back (proleptic, any year) */
static int64_t days_from_civil(int64_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t) (y - era * 400);
    uint32_t doy = (153u * (m > 2 ? m - 3u : m + 9u) + 2u) / 5u + d - 1u;
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t) doe - 719468;
}

static void civil_from_days(int64_t z, int32_t *y, uint8_t *m, uint8_t *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t) (z - era * 146097);
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    *d = (uint8_t) (doy - (153u * mp + 2u) / 5u + 1u);
    *m = (uint8_t) (mp < 10 ? mp + 3u : mp - 9u);
    *y = (int32_t) ((int64_t) yoe + era * 400 + (*m <= 2));
}

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

zui_civil_t zui_locale_civil(int64_t epoch, int32_t offset)
{
    zui_civil_t c;
    int64_t t = epoch + offset;
    int64_t days = floor_div(t, 86400);
    int64_t s = t - days * 86400;
    civil_from_days(days, &c.year, &c.month, &c.day);
    c.hour = (uint8_t) (s / 3600);
    c.minute = (uint8_t) (s / 60 % 60);
    c.second = (uint8_t) (s % 60);
    c.weekday = (uint8_t) ((days % 7 + 7 + 4) % 7); /* 1970-01-01 was a Thursday */
    return c;
}

/* The Solar Hijri calendar (the common arithmetic Persian-calendar algorithm
 * with its table of 33-year cycle breaks). Returns false out of
 * range (years before -61 or from 3178). */
static bool jal_cal(int32_t jy, int32_t *leap, int32_t *march)
{
    static const int16_t BRK[] = {-61,  9,    38,   199,  426,  686,  756,  818,  1111, 1181,
                                  1210, 1635, 2060, 2097, 2192, 2262, 2324, 2394, 2456, 3178};
    const int32_t nb = (int32_t) (sizeof BRK / sizeof BRK[0]);
    int32_t gy = jy + 621, leap_j = -14, jp = BRK[0], jump = 0, n;
    if (jy < BRK[0] || jy >= BRK[nb - 1]) return false;
    for (int32_t i = 1; i < nb; i++) {
        int32_t jm = BRK[i];
        jump = jm - jp;
        if (jy < jm) break;
        leap_j += jump / 33 * 8 + jump % 33 / 4;
        jp = jm;
    }
    n = jy - jp;
    leap_j += n / 33 * 8 + (n % 33 + 3) / 4;
    if (jump % 33 == 4 && jump - n == 4) leap_j += 1;
    int32_t leap_g = gy / 4 - (gy / 100 + 1) * 3 / 4 - 150;
    *march = 20 + leap_j - leap_g;
    if (jump - n < 6) n = n - jump + (jump + 4) / 33 * 33;
    int32_t lp = ((n + 1) % 33 - 1) % 4;
    *leap = lp == -1 ? 4 : lp;
    return true;
}

zui_civil_t zui_locale_persian(int64_t epoch, int32_t offset)
{
    zui_civil_t c = zui_locale_civil(epoch, offset);
    int32_t leap, march;
    int32_t jy = c.year - 621;
    if (!jal_cal(jy, &leap, &march)) return c; /* out of range: Gregorian */
    int64_t day = days_from_civil(c.year, c.month, c.day);
    int64_t k = day - days_from_civil(c.year, 3, (uint32_t) march);
    if (k >= 0) {
        if (k <= 185) {
            c.year = jy;
            c.month = (uint8_t) (1 + k / 31);
            c.day = (uint8_t) (k % 31 + 1);
            return c;
        }
        k -= 186;
    } else {
        jy -= 1;
        k += 179;
        if (leap == 1) k += 1;
    }
    c.year = jy;
    c.month = (uint8_t) (7 + k / 30);
    c.day = (uint8_t) (k % 30 + 1);
    return c;
}

/* zh-Hant's flexible day periods (CLDR dayPeriods for zh) */
static const char *day_period_b(uint8_t h, uint8_t m)
{
    if (h == 0 && m == 0) return "午夜";
    if (h < 5) return "凌晨";
    if (h < 8) return "清晨";
    if (h < 12) return "上午";
    if (h < 13) return "中午";
    if (h < 19) return "下午";
    return "晚上";
}

static void format_pattern(sb_t *b, const zui_locale_t *l, const char *pat, zui_civil_t c)
{
    uint8_t sys = l->num[0].sys;
    for (uint32_t i = 0; pat[i];) {
        char ch = pat[i];
        if (ch == '\'') { /* quoted literal; '' is a quote */
            i++;
            if (pat[i] == '\'') {
                sb_s(b, "'");
                i++;
                continue;
            }
            uint32_t s = i;
            while (pat[i] && pat[i] != '\'') i++;
            sb_n(b, pat + s, i - s);
            if (pat[i]) i++;
            continue;
        }
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
            uint32_t s = i;
            i++;
            while (pat[i] && ((uint8_t) pat[i] & 0xC0u) == 0x80u) i++;
            sb_n(b, pat + s, i - s);
            continue;
        }
        uint32_t run = 0;
        while (pat[i] == ch) run++, i++;
        uint8_t h12 = (uint8_t) (c.hour % 12u ? c.hour % 12u : 12u);
        switch (ch) {
        case 'y': {
            uint64_t y = (uint64_t) (c.year < 0 ? -c.year : c.year);
            if (run == 2)
                put_field(b, sys, y % 100u, 2);
            else
                put_field(b, sys, y, run);
            break;
        }
        case 'M':
            put_field(b, sys, c.month, run);
            break;
        case 'd':
            put_field(b, sys, c.day, run);
            break;
        case 'H':
            put_field(b, sys, c.hour, run);
            break;
        case 'h':
            put_field(b, sys, h12, run);
            break;
        case 'm':
            put_field(b, sys, c.minute, run);
            break;
        case 's':
            put_field(b, sys, c.second, run);
            break;
        case 'a':
            sb_s(b, c.hour < 12 ? l->am : l->pm);
            break;
        case 'B':
            sb_s(b, day_period_b(c.hour, c.minute));
            break;
        default:
            break; /* a field this short form never uses */
        }
    }
}

static zui_civil_t local_date(const zui_locale_t *l, int64_t epoch, int32_t offset)
{
    if (l->calendar == CAL_PERSIAN) return zui_locale_persian(epoch, offset);
    zui_civil_t c = zui_locale_civil(epoch, offset);
    if (l->calendar == CAL_BUDDHIST) c.year += 543;
    return c;
}

uint32_t zui_locale_date(const zui_locale_t *loc, int64_t epoch, int32_t offset, char *out,
                         uint32_t cap)
{
    const zui_locale_t *l = L_(loc);
    sb_t b = sb_start(out, cap);
    format_pattern(&b, l, l->date, local_date(l, epoch, offset));
    return b.n;
}

uint32_t zui_locale_time(const zui_locale_t *loc, int64_t epoch, int32_t offset, char *out,
                         uint32_t cap)
{
    const zui_locale_t *l = L_(loc);
    sb_t b = sb_start(out, cap);
    format_pattern(&b, l, l->time, zui_locale_civil(epoch, offset));
    return b.n;
}

uint32_t zui_locale_datetime(const zui_locale_t *loc, int64_t epoch, int32_t offset, char *out,
                             uint32_t cap)
{
    const zui_locale_t *l = L_(loc);
    sb_t b = sb_start(out, cap);
    format_pattern(&b, l, l->date, local_date(l, epoch, offset));
    sb_s(&b, " ");
    format_pattern(&b, l, l->time, zui_locale_civil(epoch, offset));
    return b.n;
}

/* ---- L7: the reader's words ------------------------------------------------------- */

const char *zui_locale_english(zui_word_t w)
{
    return (uint32_t) w < ZUI_WORD_COUNT ? W_en[w] : "";
}

const char *zui_locale_word(const zui_locale_t *loc, zui_word_t w)
{
    if ((uint32_t) w >= ZUI_WORD_COUNT) return "";
    const char *s = L_(loc)->words[w];
    return s ? s : W_en[w];
}

/* A template with {x} (text), {n} and {k} (numbers, already formatted). */
static void put_template(sb_t *b, const char *t, const char *x, const char *n, const char *k)
{
    for (uint32_t i = 0; t[i];) {
        if (t[i] == '{' && t[i + 1] && t[i + 2] == '}') {
            const char *v = t[i + 1] == 'x' ? x : t[i + 1] == 'n' ? n : t[i + 1] == 'k' ? k : 0;
            if (v) {
                sb_s(b, v);
                i += 3;
                continue;
            }
        }
        uint32_t s = i++; /* a literal run, written whole so a cut lands between characters */
        while (t[i] && t[i] != '{') i++;
        sb_n(b, t + s, i - s);
    }
}

static const char *count_template(const zui_locale_t *l, zui_count_t noun, int64_t n)
{
    zui_plural_t p = zui_locale_plural(l, n);
    const char *t = l->counts[noun][p];
    if (!t) t = l->counts[noun][ZUI_PLURAL_OTHER];
    return t;
}

static void put_count(sb_t *b, const zui_locale_t *l, zui_count_t noun, int64_t n)
{
    char num[96];
    zui_locale_integer(l, n, num, sizeof num);
    put_template(b, count_template(l, noun, n), 0, num, 0);
}

uint32_t zui_locale_count(const zui_locale_t *loc, zui_count_t noun, int64_t n, char *out,
                          uint32_t cap)
{
    sb_t b = sb_start(out, cap);
    if ((uint32_t) noun < ZUI_COUNT_NOUNS) put_count(&b, L_(loc), noun, n);
    return b.n;
}

static void put_position(sb_t *b, const zui_locale_t *l, uint64_t k, uint64_t n)
{
    char ks[96], ns[96];
    zui_locale_integer(l, (int64_t) (k > (uint64_t) INT64_MAX ? (uint64_t) INT64_MAX : k), ks,
                       sizeof ks);
    zui_locale_integer(l, (int64_t) (n > (uint64_t) INT64_MAX ? (uint64_t) INT64_MAX : n), ns,
                       sizeof ns);
    put_template(b, zui_locale_word(l, ZUI_WORD_POSITION), 0, ns, ks);
}

uint32_t zui_locale_position(const zui_locale_t *loc, uint64_t k, uint64_t n, char *out,
                             uint32_t cap)
{
    sb_t b = sb_start(out, cap);
    put_position(&b, L_(loc), k, n);
    return b.n;
}

uint32_t zui_locale_heading(const zui_locale_t *loc, uint32_t level, char *out, uint32_t cap)
{
    const zui_locale_t *l = L_(loc);
    char ns[96];
    sb_t b = sb_start(out, cap);
    zui_locale_integer(l, level, ns, sizeof ns);
    put_template(&b, zui_locale_word(l, ZUI_WORD_HEADING_LEVEL), 0, ns, 0);
    return b.n;
}

/* ---- L8: phrasing an English utterance ---------------------------------------------- */

static bool starts(const char *s, uint32_t n, const char *p)
{
    uint32_t k = slen(p);
    if (k > n) return false;
    for (uint32_t i = 0; i < k; i++)
        if (s[i] != p[i]) return false;
    return true;
}

/* A decimal number at s[*i..n): true with *v, *i moved past it. */
static bool read_num(const char *s, uint32_t n, uint32_t *i, uint64_t *v)
{
    uint32_t j = *i;
    uint64_t x = 0;
    while (j < n && s[j] >= '0' && s[j] <= '9' && j - *i < 19)
        x = x * 10u + (uint64_t) (s[j++] - '0');
    if (j == *i || (j < n && s[j] >= '0' && s[j] <= '9')) return false;
    *v = x;
    *i = j;
    return true;
}

/* "N items", "N item", ... at s[*i..n) to its end. */
static bool read_count(const char *s, uint32_t n, uint32_t i, zui_count_t *noun, uint64_t *v)
{
    static const struct {
        const char *one, *many;
        zui_count_t noun;
    } N[] = {{" item", " items", ZUI_COUNT_ITEM},
             {" character", " characters", ZUI_COUNT_CHARACTER},
             {" token", " tokens", ZUI_COUNT_TOKEN},
             {" percent", " percent", ZUI_COUNT_PERCENT}};
    if (!read_num(s, n, &i, v)) return false;
    for (uint32_t k = 0; k < sizeof N / sizeof N[0]; k++) {
        if ((starts(s + i, n - i, N[k].one) && i + slen(N[k].one) == n) ||
            (starts(s + i, n - i, N[k].many) && i + slen(N[k].many) == n)) {
            *noun = N[k].noun;
            return true;
        }
    }
    return false;
}

static int64_t clamp64(uint64_t v)
{
    return v > (uint64_t) INT64_MAX ? INT64_MAX : (int64_t) v;
}

/* Translate one part s[0..n); returns false (and writes nothing) when it
 * is not one of the reader's words or patterns. */
static bool put_part(sb_t *b, const zui_locale_t *l, const char *s, uint32_t n)
{
    for (uint32_t w = 0; w < ZUI_WORD_COUNT; w++) {
        const char *e = W_en[w];
        if (w == ZUI_WORD_ERROR_PREFIX) continue;
        bool pattern = false;
        for (const char *c = e; *c; c++)
            if (*c == '{') pattern = true;
        if (!pattern && slen(e) == n && starts(s, n, e)) {
            sb_s(b, zui_locale_word(l, (zui_word_t) w));
            return true;
        }
    }
    uint32_t i = 0;
    uint64_t a, c;
    zui_count_t noun;
    if (starts(s, n, "heading level ")) {
        i = 14;
        if (read_num(s, n, &i, &a) && i == n) {
            char num[96];
            zui_locale_integer(l, clamp64(a), num, sizeof num);
            put_template(b, zui_locale_word(l, ZUI_WORD_HEADING_LEVEL), 0, num, 0);
            return true;
        }
    }
    i = 0;
    if (read_num(s, n, &i, &a) && starts(s + i, n - i, " of ")) {
        i += 4;
        if (read_num(s, n, &i, &c) && i == n) {
            put_position(b, l, a, c);
            return true;
        }
    }
    if (read_count(s, n, 0, &noun, &a)) {
        put_count(b, l, noun, clamp64(a));
        return true;
    }
    if (starts(s, n, "about ") && read_count(s, n, 6, &noun, &a)) {
        char inner[256];
        sb_t t = sb_start(inner, sizeof inner);
        put_count(&t, l, noun, clamp64(a));
        put_template(b, zui_locale_word(l, ZUI_WORD_ABOUT), inner, 0, 0);
        return true;
    }
    static const struct {
        const char *en;
        zui_word_t w;
    } X[] = {{"from ", ZUI_WORD_FROM}, {"by ", ZUI_WORD_BY}};
    for (uint32_t k = 0; k < sizeof X / sizeof X[0]; k++) {
        uint32_t p = slen(X[k].en);
        if (n > p && starts(s, n, X[k].en)) {
            char who[ZUI_READER_UTTER];
            sb_t t = sb_start(who, sizeof who);
            sb_n(&t, s + p, n - p);
            put_template(b, zui_locale_word(l, X[k].w), who, 0, 0);
            return true;
        }
    }
    if (starts(s, n, "shortcut ")) { /* the key names are kept */
        sb_s(b, zui_locale_word(l, ZUI_WORD_SHORTCUT));
        sb_n(b, s + 8, n - 8);
        return true;
    }
    return false;
}

uint32_t zui_locale_phrase(const zui_locale_t *loc, const char *english, char *out, uint32_t cap)
{
    const zui_locale_t *l = L_(loc);
    sb_t b = sb_start(out, cap);
    const char *s = english;
    uint32_t n = slen(s);
    if (!s) return 0;
    const char *ep = W_en[ZUI_WORD_ERROR_PREFIX];
    if (starts(s, n, ep)) { /* the rest is the app's own message */
        sb_s(&b, zui_locale_word(l, ZUI_WORD_ERROR_PREFIX));
        sb_n(&b, s + slen(ep), n - slen(ep));
        return b.n;
    }
    bool first = true, prev_known = false;
    for (uint32_t i = 0; i <= n;) {
        /* the longest word ending at a part boundary (some contain ", ") */
        uint32_t end = i;
        while (end < n && !(s[end] == ',' && s[end + 1] == ' ')) end++;
        for (uint32_t w = 0; w < ZUI_WORD_COUNT; w++) {
            uint32_t k = slen(W_en[w]);
            if (k > end - i && starts(s + i, n - i, W_en[w]) &&
                (i + k == n || (s[i + k] == ',' && s[i + k + 1] == ' ')))
                end = i + k;
        }
        char part[ZUI_READER_UTTER * 2u];
        sb_t t = sb_start(part, sizeof part);
        bool known = put_part(&t, l, s + i, end - i);
        if (!first) sb_s(&b, (known || prev_known) ? l->list : ", ");
        if (known)
            sb_s(&b, part);
        else
            sb_n(&b, s + i, end - i);
        first = false;
        prev_known = known;
        if (end >= n) break;
        i = end + 2u;
    }
    return b.n;
}

/* ---- L9: the speech shim ------------------------------------------------------------- */

void zui_locale_voice_init(zui_locale_voice_t *v, const zui_locale_t *loc, zui_speak_fn next,
                           void *next_ctx)
{
    if (!v) return;
    v->loc = L_(loc);
    v->next = next;
    v->next_ctx = next_ctx;
    v->phrased = 0;
    v->buf[0] = 0;
}

void zui_locale_speak(const char *text, zui_say_t how, void *voice)
{
    zui_locale_voice_t *v = (zui_locale_voice_t *) voice;
    if (!v || !v->next) return;
    if (!text || !text[0]) {
        v->next(text ? text : "", how, v->next_ctx);
        return;
    }
    zui_locale_phrase(v->loc, text, v->buf, sizeof v->buf);
    v->phrased++;
    v->next(v->buf, how, v->next_ctx);
}
