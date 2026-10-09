/* test_holo.c — Host-testable tests for ZEDEC Holographic Data System
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../kernel/src/holographic/holo.h"

static void test_types_and_magic(void) {
    printf("=== Holographic Type System Tests ===\n");
    assert(holo_detect_type(HOLO_MAGIC_36N9) == HOLO_TYPE_36N9);
    assert(holo_detect_type(HOLO_MAGIC_9N63) == HOLO_TYPE_9N63);
    assert(holo_detect_type(HOLO_MAGIC_36M9) == HOLO_TYPE_36M9);
    assert(holo_detect_type(HOLO_MAGIC_ZEDEI) == HOLO_TYPE_ZEDEI);
    assert(holo_detect_type(HOLO_MAGIC_ZEDEC) == HOLO_TYPE_ZEDEC);
    assert(holo_detect_type(0xDEADBEEF) == HOLO_TYPE_NONE);
    printf("  [PASS] Magic number detection\n");

    assert(strcmp(holo_type_name(HOLO_TYPE_36N9), "Positive Space (36n9)") == 0);
    assert(strcmp(holo_type_name(HOLO_TYPE_9N63), "Negative Space (9n63)") == 0);
    assert(strcmp(holo_type_name(HOLO_TYPE_36M9), "Dataset (36m9)") == 0);
    assert(strcmp(holo_type_name(HOLO_TYPE_ZEDEI), "Holographic Renderer (zedei)") == 0);
    assert(strcmp(holo_type_name(HOLO_TYPE_ZEDEC), "Container (zedec)") == 0);
    printf("  [PASS] Type names\n");

    assert(strcmp(holo_type_extension(HOLO_TYPE_36N9), ".36n9") == 0);
    assert(strcmp(holo_type_extension(HOLO_TYPE_9N63), ".9n63") == 0);
    assert(strcmp(holo_type_extension(HOLO_TYPE_36M9), ".36m9") == 0);
    assert(strcmp(holo_type_extension(HOLO_TYPE_ZEDEI), ".zedei") == 0);
    assert(strcmp(holo_type_extension(HOLO_TYPE_ZEDEC), ".zedec") == 0);
    printf("  [PASS] File extensions\n");

    assert(strcmp(holo_encoding_name(HOLO_ENC_RAW), "Raw") == 0);
    assert(strcmp(holo_encoding_name(HOLO_ENC_PHASE), "Phase-encoded") == 0);
    assert(strcmp(holo_encoding_name(HOLO_ENC_INTERFERENCE), "Interference") == 0);
    assert(strcmp(holo_encoding_name(HOLO_ENC_FRACTAL), "Fractal") == 0);
    assert(strcmp(holo_encoding_name(HOLO_ENC_QUANTUM), "Quantum") == 0);
    printf("  [PASS] Encoding names\n");

    assert(strcmp(holo_dimension_name(HOLO_DIM_1D), "1D (Linear)") == 0);
    assert(strcmp(holo_dimension_name(HOLO_DIM_2D), "2D (Planar)") == 0);
    assert(strcmp(holo_dimension_name(HOLO_DIM_3D), "3D (Volumetric)") == 0);
    assert(strcmp(holo_dimension_name(HOLO_DIM_4D), "4D (Temporal-3D)") == 0);
    assert(strcmp(holo_dimension_name(HOLO_DIM_HOLO), "Holographic (Light Field)") == 0);
    printf("  [PASS] Dimension names\n");

    assert(holo_type_from_extension("test.36n9") == HOLO_TYPE_36N9);
    assert(holo_type_from_extension("data.9n63") == HOLO_TYPE_9N63);
    assert(holo_type_from_extension("set.36m9") == HOLO_TYPE_36M9);
    assert(holo_type_from_extension("render.zedei") == HOLO_TYPE_ZEDEI);
    assert(holo_type_from_extension("bundle.zedec") == HOLO_TYPE_ZEDEC);
    assert(holo_type_from_extension("readme.txt") == HOLO_TYPE_NONE);
    assert(holo_is_holo_file("image.36n9") == true);
    assert(holo_is_holo_file("image.png") == false);
    printf("  [PASS] Extension matching\n\n");
}

static void test_positive_space(void) {
    printf("=== .36n9 Positive Space Tests ===\n");
    static holo_positive_t pos;
    int32_t r = holo_positive_create(&pos, 1, 2, 255, 44000,
                                      HOLO_ENC_PHASE, HOLO_DIM_2D);
    assert(r == 0);
    assert(pos.header.magic == HOLO_MAGIC_36N9);
    assert(pos.header.type == HOLO_TYPE_36N9);
    assert(pos.positive_id == 1);
    assert(pos.pair_id == 2);
    assert(pos.amplitude == 255);
    assert(pos.frequency == 44000);
    assert(pos.header.omega == 1); /* positive */
    assert(holo_positive_validate(&pos) == 0);
    printf("  [PASS] Positive space created + validated\n");

    /* Serialization */
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[256];
    int32_t len = holo_serialize_positive(&pos, payload, 4, buf, sizeof(buf));
    assert(len > 0);
    assert((uint32_t)len == sizeof(pos) + 4);
    printf("  [PASS] Serialization: %d bytes\n", len);

    /* Deserialization */
    static holo_positive_t pos2;
    uint8_t payload_out[256];
    uint32_t plen = 0;
    r = holo_deserialize_positive(buf, len, &pos2, payload_out, &plen);
    assert(r == 0);
    assert(pos2.positive_id == 1);
    assert(pos2.pair_id == 2);
    assert(plen == 4);
    assert(payload_out[0] == 0xDE);
    assert(payload_out[3] == 0xEF);
    printf("  [PASS] Deserialization round-trip\n\n");
}

static void test_negative_space(void) {
    printf("=== .9n63 Negative Space Tests ===\n");
    static holo_negative_t neg;
    int32_t r = holo_negative_create(&neg, 2, 1, 128, 90,
                                      HOLO_ENC_INTERFERENCE, HOLO_DIM_2D);
    assert(r == 0);
    assert(neg.header.magic == HOLO_MAGIC_9N63);
    assert(neg.header.type == HOLO_TYPE_9N63);
    assert(neg.negative_id == 2);
    assert(neg.pair_id == 1);
    assert(neg.inverse_amplitude == 128);
    assert(neg.phase_offset == 90);
    assert(neg.header.omega == 0); /* negative */
    assert(holo_negative_validate(&neg) == 0);
    printf("  [PASS] Negative space created + validated\n");

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t buf[256];
    int32_t len = holo_serialize_negative(&neg, payload, 4, buf, sizeof(buf));
    assert(len > 0);
    printf("  [PASS] Serialization: %d bytes\n", len);

    static holo_negative_t neg2;
    uint8_t payload_out[256];
    uint32_t plen = 0;
    r = holo_deserialize_negative(buf, len, &neg2, payload_out, &plen);
    assert(r == 0);
    assert(neg2.negative_id == 2);
    assert(plen == 4);
    assert(payload_out[0] == 0x01);
    printf("  [PASS] Deserialization round-trip\n\n");
}

static void test_dataset(void) {
    printf("=== .36m9 Dataset Tests ===\n");
    static holo_dataset_t ds;
    int32_t r = holo_dataset_create(&ds, 100, "TestDataset");
    assert(r == 0);
    assert(ds.header.magic == HOLO_MAGIC_36M9);
    assert(ds.dataset_id == 100);
    assert(ds.num_pairs == 0);
    assert(strcmp(ds.dataset_name, "TestDataset") == 0);
    assert(holo_dataset_validate(&ds) == 0);
    printf("  [PASS] Dataset created + validated\n");

    /* Add pairs */
    r = holo_dataset_add_pair(&ds, 1, 2, 0);
    assert(r == 0);
    r = holo_dataset_add_pair(&ds, 3, 4, 1);
    assert(r == 1);
    r = holo_dataset_add_pair(&ds, 5, 6, 2);
    assert(r == 2);
    assert(ds.num_pairs == 3);
    assert(ds.pairs[0].positive_id == 1);
    assert(ds.pairs[0].negative_id == 2);
    assert(ds.pairs[0].render_order == 0);
    printf("  [PASS] Added 3 pairs\n");

    /* Pair checksum */
    uint32_t cs = holo_dataset_pair_checksum(1, 2);
    uint32_t cs2 = holo_dataset_pair_checksum(1, 2);
    assert(cs == cs2);
    uint32_t cs3 = holo_dataset_pair_checksum(2, 1);
    assert(cs != cs3);
    printf("  [PASS] Pair checksums are deterministic + order-dependent\n");

    /* Remove pair */
    r = holo_dataset_remove_pair(&ds, 1);
    assert(r == 0);
    assert(ds.num_pairs == 2);
    assert(ds.pairs[0].positive_id == 1);
    assert(ds.pairs[1].positive_id == 5);
    printf("  [PASS] Remove pair works\n");

    /* Serialization round-trip */
    uint8_t buf[8192];
    int32_t len = holo_serialize_dataset(&ds, buf, sizeof(buf));
    assert(len > 0);
    static holo_dataset_t ds2;
    r = holo_deserialize_dataset(buf, len, &ds2);
    assert(r == 0);
    assert(ds2.num_pairs == 2);
    assert(ds2.pairs[0].positive_id == 1);
    printf("  [PASS] Serialization round-trip\n\n");
}

static void test_renderer(void) {
    printf("=== .zedei Holographic Renderer Tests ===\n");
    static holo_renderer_t rend;
    int32_t r = holo_renderer_create(&rend, 200, HOLO_RENDER_HOLOGRAM,
                                      HOLO_BLEND_HOLOGRAPHIC, 1920, 1080, 100);
    assert(r == 0);
    assert(rend.header.magic == HOLO_MAGIC_ZEDEI);
    assert(rend.renderer_id == 200);
    assert(rend.render_mode == HOLO_RENDER_HOLOGRAM);
    assert(rend.blend_mode == HOLO_BLEND_HOLOGRAPHIC);
    assert(rend.target_width == 1920);
    assert(rend.target_height == 1080);
    assert(rend.wavelength == 632);
    assert(rend.dataset_id == 100);
    assert(holo_renderer_validate(&rend) == 0);
    printf("  [PASS] Renderer created + validated\n");

    assert(strcmp(holo_render_mode_name(HOLO_RENDER_HOLOGRAM), "Hologram") == 0);
    assert(strcmp(holo_render_mode_name(HOLO_RENDER_VISUAL), "Visual") == 0);
    assert(strcmp(holo_render_mode_name(HOLO_RENDER_AUDIO), "Audio") == 0);
    printf("  [PASS] Render mode names\n");

    assert(strcmp(holo_blend_mode_name(HOLO_BLEND_INTERFERENCE), "Interference") == 0);
    assert(strcmp(holo_blend_mode_name(HOLO_BLEND_FOURIER), "Fourier") == 0);
    assert(strcmp(holo_blend_mode_name(HOLO_BLEND_HOLOGRAPHIC), "Holographic") == 0);
    printf("  [PASS] Blend mode names\n");

    /* Serialization with shader */
    const char *shader = "M5:holo:reconstruct(phase=0,blend=holo)";
    uint8_t buf[4096];
    int32_t len = holo_serialize_renderer(&rend, (const uint8_t*)shader,
                                           strlen(shader), buf, sizeof(buf));
    assert(len > 0);
    static holo_renderer_t rend2;
    uint8_t shader_out[256];
    uint32_t slen = 0;
    r = holo_deserialize_renderer(buf, len, &rend2, shader_out, &slen);
    assert(r == 0);
    assert(rend2.target_width == 1920);
    assert(slen == strlen(shader));
    shader_out[slen] = 0;
    assert(strcmp((char*)shader_out, shader) == 0);
    printf("  [PASS] Serialization with shader round-trip\n\n");
}

static void test_container(void) {
    printf("=== .zedec Container Tests ===\n");
    static holo_container_t cont;
    int32_t r = holo_container_create(&cont, 300, "TestContainer",
                                       "ZEDEC:node:0001");
    assert(r == 0);
    assert(cont.header.magic == HOLO_MAGIC_ZEDEC);
    assert(cont.container_id == 300);
    assert(strcmp(cont.container_name, "TestContainer") == 0);
    assert(strcmp(cont.signature, "ZEDEC:node:0001") == 0);
    assert(cont.num_entries == 0);
    assert(holo_container_validate(&cont) == 0);
    printf("  [PASS] Container created + validated\n");

    /* Add entries */
    r = holo_container_add_36n9(&cont, "image.36n9", 0, 1024);
    assert(r == 0);
    r = holo_container_add_9n63(&cont, "image.9n63", 1024, 1024);
    assert(r == 1);
    r = holo_container_add_36m9(&cont, "set.36m9", 2048, 512);
    assert(r == 2);
    r = holo_container_add_zedei(&cont, "render.zedei", 2560, 256);
    assert(r == 3);
    assert(cont.num_entries == 4);
    assert(cont.total_size == 1024 + 1024 + 512 + 256);
    printf("  [PASS] Added 4 entries (36n9 + 9n63 + 36m9 + zedei)\n");

    /* Entry counting by type */
    assert(holo_container_entry_count(&cont, HOLO_ENTRY_36N9) == 1);
    assert(holo_container_entry_count(&cont, HOLO_ENTRY_9N63) == 1);
    assert(holo_container_entry_count(&cont, HOLO_ENTRY_36M9) == 1);
    assert(holo_container_entry_count(&cont, HOLO_ENTRY_ZEDEI) == 1);
    printf("  [PASS] Entry count by type correct\n");

    /* Serialization */
    uint8_t buf[65536];
    int32_t len = holo_serialize_container(&cont, buf, sizeof(buf));
    assert(len > 0);
    static holo_container_t cont2;
    r = holo_deserialize_container(buf, len, &cont2);
    assert(r == 0);
    assert(cont2.num_entries == 4);
    assert(cont2.container_id == 300);
    assert(strcmp(cont2.container_name, "TestContainer") == 0);
    assert(holo_container_entry_count(&cont2, HOLO_ENTRY_36N9) == 1);
    printf("  [PASS] Serialization round-trip\n\n");
}

static void test_interference_reconstruction(void) {
    printf("=== Holographic Reconstruction Tests ===\n");
    static holo_ctx_t ctx;
    holo_init(&ctx);

    /* Create positive and negative space */
    static holo_positive_t pos;
    static holo_negative_t neg;
    holo_positive_create(&pos, 1, 2, 255, 44000, HOLO_ENC_INTERFERENCE, HOLO_DIM_2D);
    holo_negative_create(&neg, 2, 1, 255, 180, HOLO_ENC_INTERFERENCE, HOLO_DIM_2D);
    ctx.positive = &pos;
    ctx.negative = &neg;
    ctx.positive_loaded = true;
    ctx.negative_loaded = true;

    /* Test payloads */
    static uint8_t pos_data[] = {0xFF, 0x00, 0xFF, 0x00, 0xAA, 0x55, 0xAA, 0x55};
    static uint8_t neg_data[] = {0x00, 0xFF, 0x00, 0xFF, 0x55, 0xAA, 0x55, 0xAA};
    ctx.positive_payload = pos_data;
    ctx.positive_payload_len = 8;
    ctx.negative_payload = neg_data;
    ctx.negative_payload_len = 8;

    /* Interference blend */
    uint8_t output[256];
    int32_t len = holo_interference(pos_data, 8, neg_data, 8, output, 256,
                                     HOLO_BLEND_INTERFERENCE);
    assert(len == 8);
    assert(output[0] == 0xFF); /* 0xFF ^ 0x00 = 0xFF */
    assert(output[1] == 0xFF); /* 0x00 ^ 0xFF = 0xFF */
    printf("  [PASS] Interference blend: XOR pattern correct\n");

    /* Phase shift blend */
    len = holo_interference(pos_data, 8, neg_data, 8, output, 256,
                            HOLO_BLEND_PHASE_SHIFT);
    assert(len == 8);
    assert(output[0] == 127); /* (0xFF + 0x00) / 2 = 127 */
    printf("  [PASS] Phase shift blend: average correct\n");

    /* Fourier blend */
    len = holo_interference(pos_data, 8, neg_data, 8, output, 256,
                            HOLO_BLEND_FOURIER);
    assert(len == 8);
    printf("  [PASS] Fourier blend: magnitude spectrum\n");

    /* Fresnel blend */
    len = holo_interference(pos_data, 8, neg_data, 8, output, 256,
                            HOLO_BLEND_FRESNEL);
    assert(len == 8);
    printf("  [PASS] Fresnel blend: diffraction pattern\n");

    /* Holographic blend */
    len = holo_interference(pos_data, 8, neg_data, 8, output, 256,
                            HOLO_BLEND_HOLOGRAPHIC);
    assert(len == 8);
    printf("  [PASS] Holographic blend: complex interference\n");

    /* Full reconstruction via context */
    len = holo_reconstruct(&ctx, output, 256);
    assert(len == 8);
    printf("  [PASS] Full reconstruction via context\n");

    /* Full render via context with renderer */
    static holo_renderer_t rend;
    holo_renderer_create(&rend, 1, HOLO_RENDER_HOLOGRAM,
                          HOLO_BLEND_HOLOGRAPHIC, 8, 1, 100);
    ctx.renderer = &rend;
    ctx.renderer_loaded = true;
    len = holo_render(&ctx, output, 256);
    assert(len == 8);
    printf("  [PASS] Full render with renderer\n\n");
}

static void test_checksum(void) {
    printf("=== Checksum Tests ===\n");
    uint8_t data[] = {1, 2, 3, 4, 5};
    uint32_t cs1 = holo_checksum(data, 5);
    uint32_t cs2 = holo_checksum(data, 5);
    assert(cs1 == cs2);
    data[0] = 2;
    uint32_t cs3 = holo_checksum(data, 5);
    assert(cs1 != cs3);
    assert(holo_checksum(data, 0) == 0); /* empty: init XOR final = 0 */
    printf("  [PASS] Checksum is deterministic + sensitive to changes\n\n");
}

static void test_full_pipeline(void) {
    printf("=== Full Holographic Pipeline Test ===\n");

    /* 1. Create positive space (.36n9) */
    static holo_positive_t pos;
    holo_positive_create(&pos, 10, 20, 200, 48000, HOLO_ENC_INTERFERENCE, HOLO_DIM_3D);
    uint8_t pos_payload[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    assert(holo_positive_validate(&pos) == 0);
    printf("  [PASS] Step 1: Created .36n9 positive space (3D, interference)\n");

    /* 2. Create negative space (.9n63) */
    static holo_negative_t neg;
    holo_negative_create(&neg, 20, 10, 200, 180, HOLO_ENC_INTERFERENCE, HOLO_DIM_3D);
    uint8_t neg_payload[] = {0x80, 0x70, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10};
    assert(holo_negative_validate(&neg) == 0);
    printf("  [PASS] Step 2: Created .9n63 negative space (3D, interference)\n");

    /* 3. Create dataset (.36m9) */
    static holo_dataset_t ds;
    holo_dataset_create(&ds, 500, "HologramDataset");
    holo_dataset_add_pair(&ds, 10, 20, 0);
    assert(ds.num_pairs == 1);
    assert(holo_dataset_validate(&ds) == 0);
    printf("  [PASS] Step 3: Created .36m9 dataset with 1 pair\n");

    /* 4. Create renderer (.zedei) */
    static holo_renderer_t rend;
    holo_renderer_create(&rend, 600, HOLO_RENDER_HOLOGRAM,
                          HOLO_BLEND_HOLOGRAPHIC, 512, 512, 500);
    const char *shader = "M5:holo:render(dim=3D,blend=holo,wave=632nm)";
    assert(holo_renderer_validate(&rend) == 0);
    printf("  [PASS] Step 4: Created .zedei renderer (hologram, 512x512)\n");

    /* 5. Create container (.zedec) */
    static holo_container_t cont;
    holo_container_create(&cont, 700, "FullHologram", "ZEDEC:node:0001");
    holo_container_add_36n9(&cont, "data.36n9", 0, 8);
    holo_container_add_9n63(&cont, "data.9n63", 8, 8);
    holo_container_add_36m9(&cont, "index.36m9", 16, sizeof(ds));
    holo_container_add_zedei(&cont, "render.zedei", 16 + sizeof(ds), strlen(shader));
    assert(cont.num_entries == 4);
    assert(holo_container_validate(&cont) == 0);
    printf("  [PASS] Step 5: Created .zedec container with all 4 entries\n");

    /* 6. Reconstruct hologram */
    static holo_ctx_t ctx;
    holo_init(&ctx);
    ctx.positive = &pos;
    ctx.negative = &neg;
    ctx.dataset = &ds;
    ctx.renderer = &rend;
    ctx.positive_payload = pos_payload;
    ctx.positive_payload_len = 8;
    ctx.negative_payload = neg_payload;
    ctx.negative_payload_len = 8;
    ctx.positive_loaded = true;
    ctx.negative_loaded = true;
    ctx.dataset_loaded = true;
    ctx.renderer_loaded = true;

    uint8_t output[256];
    int32_t rlen = holo_render(&ctx, output, 256);
    assert(rlen == 8);
    printf("  [PASS] Step 6: Holographic render produced %d bytes\n", rlen);

    /* Verify output is not just a copy of input */
    bool changed = false;
    for (int i = 0; i < 8; i++) {
        if (output[i] != pos_payload[i]) { changed = true; break; }
    }
    assert(changed);
    printf("  [PASS] Step 7: Output differs from input (interference applied)\n\n");
}

int main(void) {
    printf("=== ZEDEC pqOS Holographic Data System Tests ===\n\n");
    test_types_and_magic();
    test_positive_space();
    test_negative_space();
    test_dataset();
    test_renderer();
    test_container();
    test_interference_reconstruction();
    test_checksum();
    test_full_pipeline();
    printf("=== All ZEDEC holographic data system tests passed ===\n");
    return 0;
}
