/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_devmesh.c — dm_host_t trampolines over the bearer table
 * (br_devmesh.h, rules R1..R4). */
#include "br_devmesh.h"

br_status_t br_dm_classify(const uint8_t *frame, uint32_t len, uint8_t hint, uint8_t *cls,
                           uint8_t *flags)
{
    if (!frame || !cls || !flags) return BR_ERR_ARG;
    if (len < DM_HDR_BYTES || frame[0] != 'D' || frame[1] != 'M' || frame[2] != 1u)
        return BR_ERR_FORMAT;
    uint8_t t = frame[3];
    if (t >= BR_DM_T_M1 && t <= BR_DM_T_M3) {
        *cls = BR_CLASS_RECORD;
        *flags = BR_F_SIGNED | BR_F_ENCRYPTED;
        return BR_OK;
    }
    if (t != BR_DM_T_REC) return BR_ERR_FORMAT;
    uint8_t c = (hint == 0 || hint >= BR_CLASS_COUNT) ? (uint8_t) BR_CLASS_AI : hint;
    *cls = c;
    *flags = (uint8_t) (BR_F_AEAD | BR_F_ENCRYPTED | (c == BR_CLASS_SETTLEMENT ? BR_F_PAYMENT : 0u));
    return BR_OK;
}

static void t_random(void *ctx, uint8_t *out, uint32_t n)
{
    br_dm_t *a = (br_dm_t *) ctx;
    a->inner.random(a->inner.ctx, out, n);
}

static int t_send(void *ctx, const uint8_t to[DM_ID_BYTES], const uint8_t *frame, uint32_t len)
{
    br_dm_t *a = (br_dm_t *) ctx;
    uint8_t cls = 0, flags = 0;
    uint8_t hint = a->next_cls;
    br_status_t rc = br_dm_classify(frame, len, hint, &cls, &flags);
    if (rc == BR_OK && frame[3] == BR_DM_T_REC) a->next_cls = 0; /* R2: one send */
    if (rc == BR_OK) rc = br_send(a->tab, to, cls, flags, frame, len, &a->last_link);
    a->last_status = rc;
    if (rc != BR_OK) {
        a->refused++;
        return -1;
    }
    a->sent++;
    return 0;
}

static void t_event(void *ctx, const dm_event_t *ev)
{
    br_dm_t *a = (br_dm_t *) ctx;
    if (a->inner.on_event) a->inner.on_event(a->inner.ctx, ev);
}

static void t_request(void *ctx, const uint8_t from[DM_ID_BYTES], uint32_t req_id, uint8_t kind,
                      const uint8_t *payload, uint32_t len)
{
    br_dm_t *a = (br_dm_t *) ctx;
    if (a->inner.on_request) a->inner.on_request(a->inner.ctx, from, req_id, kind, payload, len);
}

static void t_reply(void *ctx, const uint8_t from[DM_ID_BYTES], uint32_t req_id,
                    const uint8_t *data, uint32_t len, bool final)
{
    br_dm_t *a = (br_dm_t *) ctx;
    if (a->inner.on_reply) a->inner.on_reply(a->inner.ctx, from, req_id, data, len, final);
}

static void t_money(void *ctx, const uint8_t from[DM_ID_BYTES], const dm_money_t *m)
{
    br_dm_t *a = (br_dm_t *) ctx;
    if (a->inner.on_money) a->inner.on_money(a->inner.ctx, from, m);
}

static void t_money_answer(void *ctx, const dm_money_t *m, const dm_confirm_t *c, int status)
{
    br_dm_t *a = (br_dm_t *) ctx;
    if (a->inner.on_money_answer) a->inner.on_money_answer(a->inner.ctx, m, c, status);
}

br_status_t br_dm_init(br_dm_t *a, const dm_host_t *inner, br_table_t *tab, dm_host_t *out)
{
    if (!a || !inner || !inner->random || !tab || !out) return BR_ERR_ARG;
    br_mset(a, 0, sizeof *a);
    br_mcpy(&a->inner, inner, sizeof *inner);
    a->tab = tab;
    br_mset(out, 0, sizeof *out);
    out->ctx = a;
    out->random = t_random;
    out->send = t_send;
    out->on_event = inner->on_event ? t_event : 0;
    out->on_request = inner->on_request ? t_request : 0;
    out->on_reply = inner->on_reply ? t_reply : 0;
    out->on_money = inner->on_money ? t_money : 0;
    out->on_money_answer = inner->on_money_answer ? t_money_answer : 0;
    return BR_OK;
}

void br_dm_set_class(br_dm_t *a, uint8_t cls)
{
    if (a) a->next_cls = cls;
}
