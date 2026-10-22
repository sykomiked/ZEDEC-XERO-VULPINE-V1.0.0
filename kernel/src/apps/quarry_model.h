/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quarry_model.h — Quarry, research, as a model (zxm.h): a topic searched
 * for in the Hoard and the owner's Web4 feed, the sources the owner holds
 * as numbered citations, claims that each name a citation, and a summary
 * of a topic's citations asked of the companion and held for the owner.
 * No drawing, no allocation, integers only. The rules it runs are
 * quarry_rules.h (QR1-QR4).
 *
 * zxvd methods used: search.query, files.read, web4.feed and swarm.ask.
 * zxvd has no method to search the web; Quarry does not invent one.
 *
 *   QM1 TOPICS. quarry_search asks search.query for a query of 1..8 words
 *       (QUARRY_QUERY_MAX bytes of UTF-8, not only spaces); the query is
 *       the topic, numbered 1.. in the order first searched (at most
 *       QUARRY_TOPICS), and the last searched is the current one. Its
 *       hits are kept in zxvd's rank, those whose path does not fit
 *       QUARRY_PATH_MAX passed over. A refused search keeps the hits and
 *       topic before it.
 *   QM2 THE FEED. quarry_feed reads one page of the owner's Web4 feed
 *       (web4.feed, signed envelopes only); a CONSENT envelope or an
 *       empty payload is no source and is passed over. A closed feed
 *       that lists envelopes is refused.
 *   QM3 CITING. A hit is cited with the head of its text (files.read, at
 *       most QUARRY_QUOTE bytes, cut on a character boundary), an
 *       envelope with the head of its payload; an envelope marked pii is
 *       never cited (ZXM_E_STATE). The citation takes the next number,
 *       under the current topic, only when its record fits QR3; a source
 *       already cited under the topic gives its number again and adds
 *       nothing. At most QUARRY_CITES are held.
 *   QM4 CLAIMS. A claim (1..QUARRY_CLAIM_MAX bytes of UTF-8) names a
 *       citation number, 0 for none; it is sourced by QR1, with held
 *       the citations carrying that number (QR4). Dropping a citation,
 *       only on the owner's confirmed word, leaves the claims naming it
 *       unsourced; quarry_unsourced counts them again.
 *   QM5 SOURCES PER TOPIC. quarry_topic_sources is QR4 over the topic
 *       column.
 *   QM6 SUMMARY. quarry_summarize asks the companion (swarm.ask, through
 *       zxm_ai_ask) for a summary of the current topic's citations in at
 *       most `words` words, each citation laid out as "[n] where" and its
 *       excerpt, the room shared evenly so that every source is in the
 *       prompt; QR2 is checked first and nothing is sent outside it. The
 *       answer is held (zxm.h M3) until quarry_accept or quarry_reject.
 *       Its bracketed numbers are counted (marks), and those that name
 *       no citation of the topic by QR1 (unheld); its words are counted,
 *       and long is set when they pass what was asked. Advice only. */
#ifndef QUARRY_MODEL_H
#define QUARRY_MODEL_H

#include "quarry_rules.h"
#include "zxm.h"

#define QUARRY_TOPICS    16u  /* topics searched in one session */
#define QUARRY_HITS      20u  /* hits kept of one search */
#define QUARRY_ENVS      20u  /* envelopes kept of one feed page */
#define QUARRY_CITES     64u  /* citations held */
#define QUARRY_CLAIMS    64u  /* claims written */
#define QUARRY_QUERY_MAX 256u /* search.query's q */
#define QUARRY_PATH_MAX  192u
#define QUARRY_SNIPPET   361u /* search.query's 120 characters, 3 bytes each */
#define QUARRY_QUOTE     400u /* bytes of a citation's excerpt */
#define QUARRY_CLAIM_MAX 280u
#define QUARRY_REF_MAX   QUARRY_PATH_MAX
#define QUARRY_NOT_CITED 0

enum { QUARRY_AI_SUMMARIZE, QUARRY_AI_COUNT };
extern const zxm_ai_action_t QUARRY_AI[QUARRY_AI_COUNT];

typedef struct {
    char path[QUARRY_PATH_MAX];
    int64_t size;
    int64_t mtime; /* seconds since 1970 */
    int64_t score; /* zxvd's */
    char snippet[QUARRY_SNIPPET];
} quarry_hit_t;

typedef struct {
    int64_t id;    /* the feed's, 1..4294967295 */
    int64_t stamp; /* seconds since 1970, 0 unknown */
    bool pii;
    char text[QUARRY_QUOTE + 1]; /* the head of its payload */
} quarry_env_t;

typedef struct {
    int64_t num;   /* QR1: 1..9999 */
    int64_t topic; /* 1.. */
    int64_t kind;  /* QUARRY_KIND_FILE or QUARRY_KIND_WEB4 */
    int64_t stamp;
    char ref[QUARRY_REF_MAX]; /* a Hoard path, or "web4:<id>" */
    char quote[QUARRY_QUOTE + 1];
    uint32_t qlen;
} quarry_cite_t;

typedef struct {
    char text[QUARRY_CLAIM_MAX + 1];
    int64_t cite; /* the number it names, 0 none */
    bool sourced; /* QR1, as last counted */
} quarry_claim_t;

typedef struct {
    zxm_link_t *link;
    char topic[QUARRY_TOPICS][QUARRY_QUERY_MAX + 1];
    uint32_t ntopics;
    int64_t current; /* the topic last searched, 0 none */
    quarry_hit_t hit[QUARRY_HITS];
    uint32_t nhits;
    int64_t total; /* every hit zxvd found */
    bool feed_open;
    quarry_env_t env[QUARRY_ENVS];
    uint32_t nenv;
    quarry_cite_t cite[QUARRY_CITES];
    uint32_t ncite;
    int64_t next_num;
    quarry_claim_t claim[QUARRY_CLAIMS];
    uint32_t nclaims;
    /* QM6 */
    zxm_held_t summary;
    int64_t summary_topic;
    int64_t summary_words;
    int64_t summary_sources;
    uint32_t marks;  /* bracketed numbers in the answer */
    uint32_t unheld; /* of them, naming no citation of the topic */
    uint32_t answer_words;
    bool long_answer;
} quarry_t;

void quarry_init(quarry_t *q, zxm_link_t *link);
/* QM1 */
int quarry_search(quarry_t *q, const char *query);
/* QM2: limit 1..QUARRY_ENVS */
int quarry_feed(quarry_t *q, uint32_t limit);
/* QM3: the citation's number in *num */
int quarry_cite_hit(quarry_t *q, uint32_t hit, int64_t *num);
int quarry_cite_env(quarry_t *q, uint32_t env, int64_t *num);
int quarry_drop(quarry_t *q, int64_t num, bool confirmed);
/* the index of citation `num`, or -1 */
int32_t quarry_find(const quarry_t *q, int64_t num);
/* QM4 */
bool quarry_claim_sourced(const quarry_t *q, int64_t cite);
int quarry_claim(quarry_t *q, const char *text, int64_t cite, uint32_t *idx);
uint32_t quarry_unsourced(quarry_t *q);
/* QM5 */
int64_t quarry_topic_sources(const quarry_t *q, int64_t topic);
/* QM6 */
int quarry_summarize(quarry_t *q, int64_t words);
int quarry_accept(quarry_t *q);
int quarry_reject(quarry_t *q);

#endif
