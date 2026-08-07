/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* virtio_snd.h — native virtio-sound (DeviceID 25) output over virtio-mmio.
 *
 * ONE native driver for the virtio-sound CONTRACT gives ZXV audio on every
 * conforming hypervisor. Same transport discipline as virtio_input/virtio_blk
 * (VA==PA identity map; clean-before-handoff; static page-aligned rings), with
 * the one structural difference that virtio-snd is MULTI-QUEUE: control=0,
 * event=1, tx(pcm)=2, rx=3 — and QUEUE_NOTIFY takes the queue INDEX, not 0.
 */
#ifndef VIRTIO_SND_H
#define VIRTIO_SND_H

#include <stdbool.h>

/* Probe the virtio-mmio slots for a virtio-sound device, negotiate, bring up
 * the control + tx queues, pick an OUTPUT PCM stream, and SET_PARAMS+PREPARE it.
 * Returns false (gracefully) if no device is present. */
bool virtio_snd_init(void);

/* Play the boot chime (a pure-integer stereo arpeggio). Blocks ~0.5s while the
 * one period drains — meant to run at boot bring-up, before the desktop. */
void virtio_snd_chime(void);

/* True once an output stream is prepared and ready to play. */
bool virtio_snd_ready(void);

#endif /* VIRTIO_SND_H */
