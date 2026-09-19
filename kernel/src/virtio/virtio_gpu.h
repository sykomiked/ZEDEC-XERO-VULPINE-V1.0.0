/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* virtio_gpu.h — virtio-gpu 2D scanout (VIRTIO 1.1 §5.7) over virtio-mmio.
 *
 * WHAT THIS IS, AND WHAT IT IS NOT
 * -------------------------------
 * This is a SCANOUT PATH, not acceleration. 2D-mode virtio-gpu gives the guest
 * exactly four powers: ask the host what the display geometry is, hand the host
 * a linear buffer to treat as a scanout resource, tell the host that a
 * rectangle of that buffer changed, and ask it to present. Every pixel in that
 * buffer is still produced by the CPU rasteriser above.
 *
 * So this does NOT by itself raise the graphics ceiling. Software rasterisation
 * remains the limit on fidelity; what changes is that the presentation path is
 * now a real device with a real, host-declared mode rather than a fw_cfg blob
 * pointer. 3D (VirGL / VIRTIO_GPU_F_VIRGL, the CONTEXT_CREATE / SUBMIT_3D
 * command family on capset 1/2) is the thing that would move the ceiling, and
 * it is deliberately NOT implemented here. Nothing in this file should be read
 * as claiming otherwise.
 *
 * WHY IT FITS THE EXISTING DISPLAY ABSTRACTION
 * -------------------------------------------
 * kernel/src/video/ramfb.c publishes a scanout as (linear XRGB8888 buffer,
 * width, height, stride = width*4). virtio_gpu_set_scanout() takes the SAME
 * three things in the same layout and the same pixel order, so the compositor
 * above (zxv_shell -> zxv_present) does not learn a second shape. The ONE
 * difference is the direction the geometry travels: ramfb is TOLD the size,
 * virtio-gpu REPORTS it. That is why virtio_gpu_display_size() exists and why
 * this file contains no resolution constant at all -- the size is read from
 * GET_DISPLAY_INFO, every boot, on whatever machine we woke up on.
 *
 * The other difference is that ramfb scans out continuously from guest memory
 * while virtio-gpu is an explicit-flush device: a frame is not on screen until
 * virtio_gpu_flush() has run TRANSFER_TO_HOST_2D + RESOURCE_FLUSH over it.
 *
 * FAIL CLOSED, DEGRADE CLEANLY -- AND SAY WHICH
 * --------------------------------------------
 * A machine with no virtio-gpu device is the NORMAL case, not an error path.
 * virtio_gpu_probe() returns false, says so on the console, and the caller
 * stays on ramfb. Nothing here panics, and nothing here fabricates a display.
 *
 * But "no device" and "a device that would not come up" are DIFFERENT FACTS,
 * and a bool cannot carry the difference. Collapsing them is not a cosmetic
 * problem: it is how a present, correctly identified GPU on a legacy transport
 * disappeared from the boot log as "no virtio-gpu device" (see virtio_gpu.c's
 * header for the measured registers). virtio_gpu_state() is therefore the real
 * answer and the bool is the convenience: probe() == false is only ever
 * reported as absence when the state says the scan found nothing.
 *
 * TRANSPORT GENERATIONS: both. Version 2 (modern) and version 1 (legacy) are
 * driven, because QEMU's virtio-mmio proxy defaults to force-legacy=true and a
 * plain `-device virtio-gpu-device` therefore arrives on a v1 slot. Passing
 * `-global virtio-mmio.force-legacy=false` still works and is still preferable
 * -- it is what the rest of this tree's drivers need -- but it is no longer a
 * precondition for this one to find its device.
 *
 * PORTABILITY
 * -----------
 * MMIO transport only; no architecture assembly. Ordering uses ZXV_DSB() from
 * kernel/include/zxv_barrier.h -- ramfb.c's sibling mistake of writing "dsb sy"
 * inline is what made a device driver arm64-only for no reason but spelling,
 * and virtio_snd.c still carries it. Arithmetic is 32-bit wherever a variable
 * divide or shift would otherwise lower to a libgcc helper that the ilp32
 * targets do not have.
 */
#ifndef ZXV_VIRTIO_GPU_H
#define ZXV_VIRTIO_GPU_H

#include <stdint.h>
#include <stdbool.h>

/* ---- the scanout budget ---------------------------------------------------
 * The internal self-test framebuffer is a STATIC buffer (freestanding, no
 * allocator), so its ceiling is a build-time choice, exactly as it is for
 * display.h's scanout. A device that reports a mode LARGER than this budget is
 * REFUSED with both numbers printed -- never silently clamped, because "I got
 * the mode the device declared" and "I got something else" must stay
 * distinguishable facts. Raise it deliberately, knowing the cost (4 bytes per
 * pixel):
 *     -DVGPU_MAX_W=2560 -DVGPU_MAX_H=1440      (QHD, ~14.7 MB)
 * The default covers QEMU's own virtio-gpu default mode -- MEASURED as 1024x768
 * on QEMU 6.2.0, not the 1280x800 this comment used to assert from memory; the
 * default is a QEMU version detail and the driver never depends on it -- with
 * room for a 1080p host without spending 15 MB to do it. */
#ifndef VGPU_MAX_W
#define VGPU_MAX_W 1920u
#endif
#ifndef VGPU_MAX_H
#define VGPU_MAX_H 1080u
#endif

/* Why the last probe ended the way it did. Everything from BAD_TRANSPORT down
 * to NO_SCANOUT means A DEVICE IS THERE and could not be driven -- a fault to
 * report, never a "no device" line. Only ABSENT and NO_TRANSPORT mean nothing
 * was found. Keep that split when adding a state. */
typedef enum {
    VGPU_STATE_UNPROBED = 0,     /* virtio_gpu_probe() has not run           */
    VGPU_STATE_ABSENT,           /* scanned; no mmio slot carries device id 16 */
    VGPU_STATE_NO_TRANSPORT,     /* board profile declares no virtio-mmio window */
    VGPU_STATE_BAD_TRANSPORT,    /* PRESENT; transport version neither 1 nor 2 */
    VGPU_STATE_FEATURES_FAILED,  /* PRESENT; device refused our feature set  */
    VGPU_STATE_QUEUE_FAILED,     /* PRESENT; control queue would not come up */
    VGPU_STATE_NO_DISPLAY_INFO,  /* PRESENT; GET_DISPLAY_INFO refused        */
    VGPU_STATE_NO_SCANOUT,       /* PRESENT; answered, but nothing enabled   */
    VGPU_STATE_LIVE              /* PRESENT and driving                      */
} virtio_gpu_state_t;

/* Find and bring up a virtio-gpu device: scan, handshake, control queue, and
 * GET_DISPLAY_INFO. Returns false when no device was brought up -- which covers
 * BOTH "none present" and "present but unusable", so a caller that needs to
 * report anything must ask virtio_gpu_state() which one it was. Idempotent. */
bool virtio_gpu_probe(void);

/* The reason behind the last probe, and a short human string for it. */
virtio_gpu_state_t virtio_gpu_state(void);
const char *virtio_gpu_state_str(void);

/* The mmio slot index and transport version (1 = legacy, 2 = modern) the device
 * was found on, both READ from the slot registers. False when the scan found no
 * device at all -- there is then nothing to report and nothing is written. */
bool virtio_gpu_transport(uint32_t *slot_out, uint32_t *version_out);

/* True once probe() succeeded and the control queue is live. */
bool virtio_gpu_is_live(void);

/* The display geometry READ from the device's GET_DISPLAY_INFO for the scanout
 * this driver adopted. Returns false if nothing has been read yet -- it never
 * invents a size. Writes the scanout index it came from when sc_out != NULL. */
bool virtio_gpu_display_size(uint32_t *w_out, uint32_t *h_out, uint32_t *sc_out);

/* Publish `fb` (a w*h XRGB8888 buffer, stride w*4, physically contiguous and
 * identity-mapped) as the active scanout: RESOURCE_CREATE_2D +
 * RESOURCE_ATTACH_BACKING + SET_SCANOUT. Pass the geometry the device reported
 * -- a mismatch is refused rather than scaled. 0 on success, negative on
 * failure. Same call shape as ramfb_init(). */
int virtio_gpu_set_scanout(volatile uint32_t *fb, uint32_t w, uint32_t h);

/* Present a dirty rectangle: TRANSFER_TO_HOST_2D then RESOURCE_FLUSH. Until
 * this runs, edits to the backing buffer are NOT on screen -- the one real
 * behavioural difference from ramfb. Returns 0 on success. */
int virtio_gpu_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Present the whole scanout. */
int virtio_gpu_flush_all(void);

/* Self-check, per the project pattern. Verifies the wire-format invariants that
 * do not need a device (header size/offsets, budget sanity) and, when a device
 * IS live, that the geometry came from the device rather than from a constant.
 * Returns the number of checks that PASSED; *n_out receives how many ran, so a
 * caller can tell "all passed" from "nothing ran". */
uint32_t virtio_gpu_selfcheck(uint32_t *n_out);

#endif /* ZXV_VIRTIO_GPU_H */
