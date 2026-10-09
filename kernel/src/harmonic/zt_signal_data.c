/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_signal_data.c — forwarding facade. See zt_signal_data.h. */
#include "zt_signal_data.h"

void zt_core_init_bank(zt_filter_bank_t *bank)
{
    zt_trunk_bank_init(bank);
}

void zt_core_pack_frame(uint8_t trunk_id, const uint32_t s_plus[3], const uint32_t s_minus[2],
                        zt_frame_t *dst_frame)
{
    zt_wire_pack_ubh168(trunk_id, s_plus, s_minus, dst_frame);
}

void zt_core_unpack_frame(const zt_frame_t *src_frame, zt_dual_rail_t *dst_rail)
{
    zt_wire_unpack_ubh168(src_frame, dst_rail);
}

zt_paraconsistent_state_t zt_core_resolve_interference(const zt_dual_rail_t *rail,
                                                       int32_t threshold_q16)
{
    return zt_wire_resolve_rails(rail, threshold_q16);
}

zt_paraconsistent_state_t zt_core_resolve_step(zt_dual_rail_t *rail, int32_t threshold_q16)
{
    return zt_wire_resolve_step(rail, threshold_q16);
}

void zt_core_demux_stream(zt_filter_bank_t *bank, const int16_t *pcm_in, size_t sample_count)
{
    zt_trunk_bank_process_pcm(bank, pcm_in, sample_count);
}
