# 2026-10-15: the x86_64 golden disc under KVM

- **Built from:** commit `475b7dee` (golden disc, with both AI models on it).
- **Booted on:** the project's test server, an Intel Xeon E5-2699 v4, in
  QEMU 6.2.0 with nested KVM, under OVMF UEFI firmware. The disc was
  attached as a SATA hard disk and an e1000 network card was present. This
  is a KVM guest, not a boot on physical hardware.
- **Disc SHA-256:** `b795c602fd4dbab436db6a8d55da38693a6b096bb6258ee5abfbe6dc7971caeb`

## Markers

| Marker | Result |
|---|---|
| `[HANDOFF_OK]` handoff v2 accepted, kernel window verified free | pass |
| `[BOOTMEM]` buffers from firmware-usable memory | pass (3 ranges, 1017 MiB) |
| `[MODELS_OK]` both AI models on the disc verified | pass (2 verified) |
| `[SCHED_OK]` ring-3 processes under the preemptive scheduler | pass |
| `[AI_OK]` swarm and tensor self-check | pass |
| `All systems online` | pass |
| `[BOOT_OK]` main loop reached, no fault | pass |
| `heartbeat: ticks=` timer interrupts in the idle loop | pass (ticks 1261 to 8261, 8 beats) |
| fault markers (`[FAULT]`, `PANIC`, exception) | none |

With these devices, the log also shows the class drivers working. The AHCI
controller reads the disc's GPT and mounts its FAT32 system partition. The
e1000 network card brings its link up and receives a DHCP offer.

## Files

| File | What it shows |
|---|---|
| [x86_64-serial.log](x86_64-serial.log) | full serial console |
| [x86_64-1-firmware-15s.png](x86_64-1-firmware-15s.png) | UEFI firmware starting the disc |
| [x86_64-2-boot-done-60s.png](x86_64-2-boot-done-60s.png) | the end of boot and the first heartbeat |
| [x86_64-3-heartbeat-120s.png](x86_64-3-heartbeat-120s.png) | heartbeats still arriving at 120 s |
