/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_devmesh.h — the adapter that lets the device mesh (kernel/src/devmesh)
 * send through the bearer table without any change to devmesh.
 *
 * HOW IT PLUGS IN
 *   devmesh's only transport hook is dm_host_t.send(ctx, to, frame, len)
 *   (devmesh.h, TRANSPORT); incoming frames are fed to dm_receive(). The
 *   adapter takes the host's own dm_host_t and returns one in which
 *     send()  -> br_send() on the bearer table (selection + failover), and
 *     every other callback -> the host's callback with the host's ctx
 *   (trampolines, because dm_host_t has one ctx for all callbacks).
 *   devmesh code is not modified; it never sees the bearers.
 *
 * WHAT CLASS A DEVMESH FRAME IS
 *   Frames are opaque to the host, so the adapter decides:
 *   R1  Handshake frames (header 'D' 'M' 1 type 1..3) are BR_CLASS_RECORD
 *       flagged SIGNED | ENCRYPTED (they carry signatures and KEM
 *       ciphertexts). They are up to 98 KB, so LoRa refuses them by size.
 *   R2  Record frames (type 4) get the class the host set with
 *       br_dm_set_class() before calling a devmesh function, flagged
 *       AEAD | ENCRYPTED (plus PAYMENT for SETTLEMENT). The default is
 *       BR_CLASS_AI, the most restricted, so a frame of unknown purpose
 *       never goes over LoRa. br_dm_set_class() lasts for one send.
 *   R3  Anything that is not a well-formed devmesh header is refused
 *       (DM_ERR_TRANSPORT reaches devmesh as a send failure).
 *   R4  HAM_SDR never carries devmesh frames: they are encrypted, and the
 *       HAM gate refuses encrypted bytes. Emergency text for the ham bands
 *       goes through br_ham.h instead.
 *
 * RECEIVING
 *   Each link's host code reassembles its own framing (br_frag, COBS,
 *   length prefix) and calls dm_receive(); nothing here is needed.
 */
#ifndef ZXV_BR_DEVMESH_H
#define ZXV_BR_DEVMESH_H

#include "bearer.h"
#include "../devmesh/devmesh.h"

typedef struct {
    dm_host_t inner; /* the host's own callbacks */
    br_table_t *tab;
    uint8_t next_cls; /* 0 = default */
    uint16_t last_link;
    int32_t last_status;
    uint32_t sent, refused;
} br_dm_t;

#define BR_DM_T_M1  1u
#define BR_DM_T_M3  3u
#define BR_DM_T_REC 4u

/* Build `out` (to pass to dm_init) from the host's dm_host_t. */
br_status_t br_dm_init(br_dm_t *a, const dm_host_t *inner, br_table_t *tab, dm_host_t *out);
/* R2: class for the next record frame sent. */
void br_dm_set_class(br_dm_t *a, uint8_t cls);
/* R1..R3: class and flags for one frame; BR_ERR_FORMAT if not devmesh. */
br_status_t br_dm_classify(const uint8_t *frame, uint32_t len, uint8_t hint, uint8_t *cls,
                           uint8_t *flags);

#endif /* ZXV_BR_DEVMESH_H */
