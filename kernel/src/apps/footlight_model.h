/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* footlight_model.h — Footlight, presentations, as a model (zxm.h): a
 * deck kept as a text file in the Hoard, each slide fitted to the window,
 * a rehearsal timed against the talk's length, the show stepped in its run
 * order, a printed handout, and the deck presented to others in a watch
 * hall. The rules are footlight_rules.h (FL1-FL8), the same ones the app's
 * KRON, C++, Trebago, BASIC and MYRA blocks run. No drawing, no
 * allocation, integers only.
 *
 *   FM1 A DECK is UTF-8 text, at most FOOTLIGHT_TEXT_MAX bytes, read in
 *       pieces with files.read and written with files.write. A line
 *       starting "# " begins a slide and is its title (at most
 *       FOOTLIGHT_TITLE bytes); a line "@ <seconds>" is the seconds the
 *       slide was rehearsed for (1..FOOTLIGHT_SECS_MAX, the last one
 *       counts); every other line is the slide's text. Text before the
 *       first title is a slide with no title. At most FOOTLIGHT_SLIDES
 *       slides. A deck that does not fit these is refused whole, and the
 *       deck shown before is kept.
 *   FM2 LAYOUT. A slide with a title and text is FOOTLIGHT_LAYOUT_BOTH,
 *       a title alone _TITLE, text alone _TEXT, neither _BLANK; its
 *       record (deck, place, seconds, layout, bytes of text, stamp) fits
 *       ^ZXFOOTLIGHT (FL7) or it is not filed.
 *   FM3 THE FIT. A window of winw by winh pixels shows the slide at
 *       kinetoscope.video_fit_w by kinetoscope.video_fit_h of a 16 by 9
 *       picture (FL1, FL2), the bars before it letterbox_bar of each
 *       side (FL3).
 *   FM4 REHEARSAL. A slide not rehearsed has 0 seconds. The rehearsal's
 *       total against the talk (FL4) needs every slide rehearsed and a
 *       talk length set; it is shown as h:mm:ss by halloo.hms (FL8),
 *       with a sign when over or under.
 *   FM5 RUN ORDER. The slide shown is 1..n (0: the show has ended or not
 *       begun); a step moves it by next_slide (FL6) in the mode set.
 *   FM6 HANDOUT. A PDF of US Letter pages, one slide to a page in
 *       Courier: its place, title and rehearsed time (when it has one) as
 *       a 14-point heading, then its text in 11 points, every line on a
 *       14-point leading from one inch in, cut at FOOTLIGHT_HANDOUT_COLS
 *       characters, a slide longer than a page going on over the next
 *       (at most FOOTLIGHT_HANDOUT_PAGES pages). Written with files.write
 *       and printed with print.submit (at most FOOTLIGHT_COPIES_MAX
 *       copies). zxvd does not convert documents (print.submit takes the
 *       formats the printer takes), and PDF is the one printers take
 *       most, so the PDF is Footlight's own. US-ASCII only: a character
 *       past it is shown as '?'.
 *   FM7 THE WATCH HALL. A hall is made with pspace.create (kind hall,
 *       governed by its owner: only the presenter steers), and only on
 *       the owner's confirmed word, since a hall is public to whoever
 *       holds its CID. The deck's media CID is loaded with hall.load; the
 *       hall's position for slide k is 1000 times where slide k starts in
 *       the run (slide_start, FL5), an unrehearsed slide holding one
 *       second there; hall.seek shows the slide, hall.status follows the
 *       presenter (the slide whose start is the last at or before the
 *       position). zxvd's refusals are shown as is.
 *   FM8 Every action checks its arguments and the answer before it
 *       changes anything; a refused one leaves the model as it was. */
#ifndef FOOTLIGHT_MODEL_H
#define FOOTLIGHT_MODEL_H

#include "footlight_rules.h"
#include "zxm.h"

#define FOOTLIGHT_SLIDES        64u    /* slides in one deck here */
#define FOOTLIGHT_TEXT_MAX      16384u /* bytes of one deck */
#define FOOTLIGHT_TITLE         79u
#define FOOTLIGHT_PATH_MAX      255u
#define FOOTLIGHT_URI_MAX       511u /* print.submit's uri */
#define FOOTLIGHT_COPIES_MAX    999u
#define FOOTLIGHT_CID_MAX       79u /* a space CID (hall.*'s cid) */
#define FOOTLIGHT_MEDIA_MAX     64u /* hall.load's media */
#define FOOTLIGHT_HALL_NAME     48u /* pspace.create's name */
#define FOOTLIGHT_STATE_MAX     16u
#define FOOTLIGHT_HALL_SECS     1   /* FM7: an unrehearsed slide in the hall */
#define FOOTLIGHT_HANDOUT_COLS  70u /* FM6: 11-point Courier across 6.5 inches */
#define FOOTLIGHT_HANDOUT_ROWS  47u /* lines of 14 points from 10 inches to 1 */
#define FOOTLIGHT_HANDOUT_PAGES 512u

enum {
    FOOTLIGHT_LAYOUT_TITLE = 0,
    FOOTLIGHT_LAYOUT_BOTH = 1,
    FOOTLIGHT_LAYOUT_TEXT = 2,
    FOOTLIGHT_LAYOUT_BLANK = 3
};
enum { FOOTLIGHT_HALL_NONE = 0, FOOTLIGHT_HALL_STOP, FOOTLIGHT_HALL_PAUSE, FOOTLIGHT_HALL_PLAY };

typedef struct {
    char title[FOOTLIGHT_TITLE + 1u];
    uint32_t body_at, body_len; /* in body[] */
    int64_t secs;               /* 0: not rehearsed */
    uint8_t layout;             /* FM2 */
} footlight_slide_t;

typedef struct {
    int64_t winw, winh; /* the window */
    int64_t w, h;       /* the slide in it (FL1, FL2) */
    int64_t left, top;  /* the bars before it (FL3) */
} footlight_fit_t;

typedef struct {
    bool set;
    int32_t state; /* FOOTLIGHT_HALL_* */
    char media[FOOTLIGHT_MEDIA_MAX + 1u];
    int64_t pos_ms;
    int64_t viewers;
    bool may_control;
} footlight_hall_t;

typedef struct {
    zxm_link_t *link;
    char path[FOOTLIGHT_PATH_MAX + 1u]; /* "" until a deck is opened or saved */
    int64_t deck;                       /* the deck's number in ^ZXFOOTLIGHT */
    footlight_slide_t slide[FOOTLIGHT_SLIDES];
    uint32_t n;
    char body[FOOTLIGHT_TEXT_MAX + 2u]; /* a last line gains its newline, then a NUL */
    uint32_t body_len;
    int64_t start_ms[FOOTLIGHT_SLIDES]; /* FM7: each slide in the hall */
    uint32_t cur;                       /* FM5 */
    int64_t mode;
    int64_t talk; /* seconds; 0: not set */
    footlight_fit_t fit;
    int64_t job; /* the handout's print job; 0: none */
    char job_state[FOOTLIGHT_STATE_MAX];
    char hall_cid[FOOTLIGHT_CID_MAX + 1u];
    footlight_hall_t hall;
} footlight_t;

void footlight_init(footlight_t *f, zxm_link_t *link);
/* FM1: a deck from text (no zxvd), and from the Hoard */
int footlight_set_text(footlight_t *f, const char *text, uint32_t len);
int footlight_open(footlight_t *f, const char *path);
/* FM1: the deck as text into out (cap bytes, with its NUL); its length or -1 */
int32_t footlight_text(const footlight_t *f, char *out, uint32_t cap);
int footlight_save(footlight_t *f, const char *path);
/* FM2: the slide's record (place 1..n) at stamp ms fits ^ZXFOOTLIGHT */
bool footlight_record_fits(const footlight_t *f, uint32_t place, int64_t stamp_ms);
int footlight_set_deck(footlight_t *f, int64_t deck);

/* FM3 */
int footlight_fit_window(footlight_t *f, int64_t winw, int64_t winh);

/* FM4: seconds 0 marks a slide not rehearsed */
int footlight_set_secs(footlight_t *f, uint32_t place, int64_t secs);
int footlight_set_talk(footlight_t *f, int64_t talk);
int footlight_rehearsal(const footlight_t *f, int64_t *over);
/* seconds (either sign) as "h:mm:ss", "-h:mm:ss" under; its length or -1 */
int32_t footlight_clock(int64_t secs, char *out, uint32_t cap);

/* FM5 */
int footlight_go(footlight_t *f, uint32_t place);
int footlight_set_mode(footlight_t *f, int64_t mode);
int footlight_step(footlight_t *f);

/* FM6: the handout's PDF (path ends ".pdf"), then printing it */
int footlight_handout(footlight_t *f, const char *path);
/* the last handout composed, and its length */
const char *footlight_handout_pdf(uint32_t *len);
int footlight_print(footlight_t *f, const char *uri, const char *path, uint32_t copies);

/* FM7 */
int footlight_hall_create(footlight_t *f, const char *name, bool owner_confirmed);
int footlight_hall_set(footlight_t *f, const char *cid);
int footlight_hall_load(footlight_t *f, const char *media);
int footlight_hall_show(footlight_t *f);
int footlight_hall_follow(footlight_t *f);
/* the slide (1..n) at a hall position, 0 when there is no deck */
uint32_t footlight_slide_at(const footlight_t *f, int64_t pos_ms);

#endif
