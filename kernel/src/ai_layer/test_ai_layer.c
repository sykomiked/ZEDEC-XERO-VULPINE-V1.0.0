/* test_ai_layer.c — AI Integration & Remote Compute tests
 *
 * Tests model registration, signature verification, local inference,
 * remote compute offload with Porter House gating, timeout detection,
 * peer management, and M5 coverage.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#include "ai_layer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../robin_debanks/crypto_verify.h"

static int feq(double a, double b, double eps) {
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

static word168_t make_peer(uint8_t seed) {
    word168_t w;
    for (int i = 0; i < WORD168_OCTETS; i++) w.bytes[i] = (uint8_t)(seed + i);
    return w;
}

static uint8_t zero_sig[AI_SIG_LEN];
static uint8_t pubkey[AI_PUBKEY_LEN];
static uint8_t hash[AI_CONTENT_HASH_LEN];

/* Compute a valid HMAC-SHA256 AI layer signature for test models */
static void compute_ai_sig(const char *name, const uint8_t *pubkey, uint8_t sig[AI_SIG_LEN]) {
    uint8_t msg[128]; uint32_t pos = 0;
    for (uint32_t i = 0; name[i] && i < 64 && pos < sizeof(msg); i++) msg[pos++] = (uint8_t)name[i];
    for (uint32_t i = 0; i < 32 && pos < sizeof(msg); i++) msg[pos++] = pubkey[i];
    uint8_t hmac_out[32];
    crypto_hmac_sha256(msg, pos, CRYPTO_AUTHORITY_KEY_AI_LAYER, hmac_out);
    for (uint32_t i = 0; i < 32; i++) sig[i] = hmac_out[i];
    for (uint32_t i = 32; i < AI_SIG_LEN; i++) sig[i] = 0;
}

int main(void) {
    memset(zero_sig, 0, AI_SIG_LEN);
    memset(pubkey, 0x42, AI_PUBKEY_LEN);
    memset(hash, 0x77, AI_CONTENT_HASH_LEN);

    /* ===== init ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        ai_engine_t ai;
        ai_init(&ai, 1, "ZEDEC:ai-layer", &ph);
        assert(ai.device_id == 1);
        assert(ai.num_models == 0);
        assert(ai.num_tasks == 0);
        assert(ai.porter == &ph);
    }

    /* ===== register model ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        ai_engine_t ai;
        ai_init(&ai, 1, "ai", &ph);

        word168_t provider = make_peer(1);
        uint8_t sig1[AI_SIG_LEN]; compute_ai_sig("QuantumLLM", pubkey, sig1);
        int32_t id = ai_register_model(&ai, "QuantumLLM", AI_MODEL_LLM,
                                         &provider, pubkey, hash, sig1,
                                         7000000000ULL, 4000000000ULL);
        assert(id > 0);
        assert(ai.num_models == 1);

        ai_model_t *m = ai_get_model(&ai, (uint32_t)id);
        assert(m != NULL);
        assert(m->state == AI_MODEL_LOADED);
        assert(m->sig_verified == true); /* HMAC verified */
        assert(m->param_count == 7000000000ULL);
    }

    /* ===== register model with bad signature ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        ai_engine_t ai;
        ai_init(&ai, 1, "ai", &ph);

        word168_t provider = make_peer(2);
        int32_t id = ai_register_model(&ai, "BadModel", AI_MODEL_VISION,
                                         &provider, pubkey, hash, zero_sig,
                                         1000, 500);
        assert(id == -2);
        ai_model_t *m = ai_get_model(&ai, 1);
        assert(m->state == AI_MODEL_REJECTED);
    }

    /* ===== local inference ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        ai_engine_t ai;
        ai_init(&ai, 1, "ai", &ph);

        word168_t provider = make_peer(3);
        uint8_t sig3[AI_SIG_LEN]; compute_ai_sig("VisionModel", pubkey, sig3);
        int32_t mid = ai_register_model(&ai, "VisionModel", AI_MODEL_VISION,
                                          &provider, pubkey, hash, sig3,
                                          500000, 200000);
        assert(mid > 0);

        uint8_t input[] = { 0x01, 0x02, 0x03, 0x04 };
        int32_t tid = ai_submit_local(&ai, (uint32_t)mid, input, 4, 100);
        assert(tid > 0);

        ai_task_t *t = ai_get_task(&ai, (uint32_t)tid);
        assert(t->state == AI_TASK_QUEUED);
        assert(t->mode == AI_TASK_LOCAL);

        assert(ai_execute_local(&ai, (uint32_t)tid, 110) == 0);
        assert(t->state == AI_TASK_COMPLETE);
        assert(t->output_len == 1);
        assert(t->output[0] == 4); /* input_len & 0xFF */
        assert(t->cycles_consumed == 11); /* 110 - 100 + 1 */

        ai_model_t *m = ai_get_model(&ai, (uint32_t)mid);
        assert(m->inference_count == 1);
        assert(ai.total_inferences == 1);
        assert(ai.total_local_inferences == 1);
    }

    /* ===== remote compute with Porter House TRUSTED ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, AI_REMOTE_PORT, PH_SEAL_TRUSTED, 500);

        ai_engine_t ai;
        ai_init(&ai, 1, "ai", &ph);

        word168_t provider = make_peer(4);
        uint8_t sig4[AI_SIG_LEN]; compute_ai_sig("RemoteLLM", pubkey, sig4);
        int32_t mid = ai_register_model(&ai, "RemoteLLM", AI_MODEL_LLM,
                                          &provider, pubkey, hash, sig4,
                                          1000000000, 500000000);
        assert(mid > 0);

        word168_t peer = make_peer(10);
        assert(ai_add_peer(&ai, &peer, 800, 8000000000ULL) == 0);
        assert(ai.num_peers == 1);

        uint8_t input[] = "hello world";
        int32_t tid = ai_submit_remote(&ai, (uint32_t)mid, &peer,
                                         input, 11, 200);
        assert(tid > 0);

        ai_task_t *t = ai_get_task(&ai, (uint32_t)tid);
        assert(t->mode == AI_TASK_REMOTE);
        assert(t->state == AI_TASK_QUEUED);

        uint8_t output[] = { 0xFF, 0xEE, 0xDD };
        assert(ai_complete_remote(&ai, (uint32_t)tid, output, 3, 250) == 0);
        assert(t->state == AI_TASK_COMPLETE);
        assert(t->output_len == 3);
        assert(t->output[0] == 0xFF);

        assert(ai.total_remote_inferences == 1);
    }

    /* ===== remote compute rejected by Porter House (low trust) ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, AI_REMOTE_PORT, PH_SEAL_TRUSTED, 500);

        ai_engine_t ai;
        ai_init(&ai, 1, "ai", &ph);

        word168_t provider = make_peer(5);
        uint8_t sig5[AI_SIG_LEN]; compute_ai_sig("Model", pubkey, sig5);
        int32_t mid = ai_register_model(&ai, "Model", AI_MODEL_LLM,
                                          &provider, pubkey, hash, sig5,
                                          100, 50);
        assert(mid > 0);

        word168_t peer = make_peer(20);
        /* Low trust peer: Porter House rejects */
        assert(ai_add_peer(&ai, &peer, 100, 1000000) == -2);
    }

    /* ===== remote task timeout ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, AI_REMOTE_PORT, PH_SEAL_OPEN, 0);

        ai_engine_t ai;
        ai_init(&ai, 1, "ai", &ph);

        word168_t provider = make_peer(6);
        uint8_t sig6[AI_SIG_LEN]; compute_ai_sig("Model", pubkey, sig6);
        int32_t mid = ai_register_model(&ai, "Model", AI_MODEL_LLM,
                                          &provider, pubkey, hash, sig6,
                                          100, 50);
        assert(mid > 0);

        word168_t peer = make_peer(30);
        assert(ai_add_peer(&ai, &peer, 0, 1000000) == 0);

        int32_t tid = ai_submit_remote(&ai, (uint32_t)mid, &peer, NULL, 0, 0);
        assert(tid > 0);

        /* Not yet timed out */
        assert(ai_check_timeouts(&ai, 1000) == 0);

        /* Times out */
        assert(ai_check_timeouts(&ai, AI_TASK_TIMEOUT_CYCLES + 100) == 1);

        ai_task_t *t = ai_get_task(&ai, (uint32_t)tid);
        assert(t->state == AI_TASK_TIMEOUT);
        assert(ai.total_timeouts == 1);
    }

    /* ===== model types ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        ai_engine_t ai;
        ai_init(&ai, 1, "ai", &ph);

        word168_t provider = make_peer(7);
        uint8_t sigL[AI_SIG_LEN]; compute_ai_sig("LLM", pubkey, sigL);
        uint8_t sigV[AI_SIG_LEN]; compute_ai_sig("Vision", pubkey, sigV);
        uint8_t sigA[AI_SIG_LEN]; compute_ai_sig("Audio", pubkey, sigA);
        uint8_t sigP[AI_SIG_LEN]; compute_ai_sig("Predict", pubkey, sigP);
        uint8_t sigE[AI_SIG_LEN]; compute_ai_sig("Embed", pubkey, sigE);

        int32_t id1 = ai_register_model(&ai, "LLM", AI_MODEL_LLM,
                                          &provider, pubkey, hash, sigL, 1, 1);
        int32_t id2 = ai_register_model(&ai, "Vision", AI_MODEL_VISION,
                                          &provider, pubkey, hash, sigV, 1, 1);
        int32_t id3 = ai_register_model(&ai, "Audio", AI_MODEL_AUDIO,
                                          &provider, pubkey, hash, sigA, 1, 1);
        int32_t id4 = ai_register_model(&ai, "Predict", AI_MODEL_PREDICTIVE,
                                          &provider, pubkey, hash, sigP, 1, 1);
        int32_t id5 = ai_register_model(&ai, "Embed", AI_MODEL_EMBEDDING,
                                          &provider, pubkey, hash, sigE, 1, 1);

        assert(id1 > 0 && id2 > 0 && id3 > 0 && id4 > 0 && id5 > 0);
        assert(ai.num_models == 5);

        assert(ai_get_model(&ai, (uint32_t)id1)->type == AI_MODEL_LLM);
        assert(ai_get_model(&ai, (uint32_t)id2)->type == AI_MODEL_VISION);
        assert(ai_get_model(&ai, (uint32_t)id3)->type == AI_MODEL_AUDIO);
        assert(ai_get_model(&ai, (uint32_t)id4)->type == AI_MODEL_PREDICTIVE);
        assert(ai_get_model(&ai, (uint32_t)id5)->type == AI_MODEL_EMBEDDING);
    }

    /* ===== coverage computation ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, AI_REMOTE_PORT, PH_SEAL_OPEN, 0);
        ai_engine_t ai;
        ai_init(&ai, 1, "ai", &ph);

        word168_t provider = make_peer(8);
        uint8_t sig8[AI_SIG_LEN]; compute_ai_sig("Model", pubkey, sig8);
        int32_t mid = ai_register_model(&ai, "Model", AI_MODEL_LLM,
                                          &provider, pubkey, hash, sig8, 1, 1);
        assert(mid > 0);

        /* Complete one local task */
        int32_t t1 = ai_submit_local(&ai, (uint32_t)mid, NULL, 0, 0);
        ai_execute_local(&ai, (uint32_t)t1, 10);

        /* Submit a queued task (in progress) */
        ai_submit_local(&ai, (uint32_t)mid, NULL, 0, 10);

        ai_update_coverage(&ai);
        /* r = 1 completed / 1 resolved = 1.0
         * ell = 1 queued / 2 total = 0.5 */
        assert(feq(ai.m5.r, 1.0, 1e-9));
        assert(feq(ai.m5.ell, 0.5, 1e-9));
    }

    printf("All AI Integration Layer tests passed\n");
    return 0;
}
