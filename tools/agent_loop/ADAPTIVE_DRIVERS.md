# The adaptive drivers and the three-anchor cross-compilation

Two pieces complete the bare-metal story: hardware drivers that
**self-adapt** to any chipset (because the lattice makes a device a point
with a derivable driver), and a cross-compilation that runs from **three
anchors at once** — the user space, the system space, and the agent.

## 1. Self-adapting hardware drivers

The traditional problem: a new chipset needs a new driver, written by
hand, and the OS cannot boot until it exists. The lattice removes the
"new": a device is a **point** in the hardware space, and the driver is a
**projection** of that point through the same elevator the languages use.

### How it works

1. **Enumerate**: the boot firmware (UEFI/BIOS/device tree) hands the
   kernel the device list — vendor, device, class, the BARs, the
   capabilities. Each is an exact integer tuple.
2. **Lift**: the tuple lifts to a point in the hardware lattice (the same
   Zeckendorf ladder, a different projection).
3. **Project**: the point projects to a driver — either an existing
   driver whose point is nearest, or a **generated** one composed from
   the driver primitives (the register read/write, the DMA ring, the
   interrupt ack, the poll loop) that the lattice orders.
4. **Verify**: the driver runs its own self-test (a register read-back, a
   loopback, a known-answer) before the kernel trusts it. A driver that
   fails is **refused**, never used — the same fail-closed discipline as
   the manifest loader.

### Why the lattice makes it possible

| Without the lattice | With it |
|---|---|
| a new chipset needs a hand-written driver | a new chipset is a new point; the driver is a projection |
| the driver list is a fixed table | the driver is **composed** from ordered primitives |
| an unknown device is unsupported | an unknown device projects to the nearest known point, or a generated composition |
| the OS cannot boot without the driver | the driver is generated at boot, then self-tested |

The primitives are finite (the PCIe class drivers already in the tree:
nvme, ahci, xhci, virtio, e1000, igb, igc, r8169, vmx3), so the
composition space is bounded and every composition is **testable**.

### The three spaces, per driver

- *positive*: a known device's driver loads and passes its self-test;
- *negative*: an unknown device is refused or generated-and-refused, never
  silently mis-driven;
- *neutral*: the boundary devices (a device with no BARs, a zero-vendor
  entry) behave deterministically.

## 2. The three-anchor cross-compilation

The user's distinction, made precise: cross-compilation is not only
"compile this code for that target". It runs from **three anchors at
once**, and a feature is complete only when all three agree.

```
anchor 1: THE USER SPACE     the feature as the human uses it
                             (the click, the view, the input, the desk)
anchor 2: THE SYSTEM SPACE   the feature as the kernel implements it
                             (the module, the syscall, the driver, the layer)
anchor 3: THE AGENT          the feature as the AI composes it
                             (the H100 drafting the code, the AIPI surface)
```

**The cross-compilation** builds each target (x86_64, arm64, riscv64,
riscv32, arm32, wasm32, the three desktops) **from all three anchors**:

- from the **user space**: the feature's UI contract is compiled into the
  target's interface (the framebuffer, the DOM, the AppKit window);
- from the **system space**: the feature's kernel contract is compiled
  into the target's modules and syscalls;
- from the **agent**: the feature's code is composed by the H100 and
  verified against both other anchors.

A feature is **100% working** when the three anchors agree: what the user
sees, what the system does, and what the agent generated are the **same
state machine** — the dual-plane parity, extended to a **tri-plane**
parity.

### The build matrix

```
            x86_64  arm64  riscv64  riscv32  arm32  wasm32  mac  win  linux
user space     .      .       .        .       .       .      .    .     .
system space   .      .       .        .       .       .      .    .     .
agent          .      .       .        .       .       .      .    .     .
```

Every cell must build and pass. A cell that builds but disagrees with
another anchor is a **gap** (the tri-plane parity fails), not a success.

### Why "a masterpiece"

A masterpiece in this sense is not one artifact but **one identity across
all artifacts**: the same lattice point, the same computed checks, the
same verified bytes, on nine targets and from three anchors. The
cross-compilation is the act of *proving* the identity, target by target
and anchor by anchor — and the proof is the byte-identity test: the same
input yields the same output everywhere.

## 3. What the H100 must build and test

1. **The hardware lift/project**: `hw_lift(device_tuple) -> point` and
   `hw_project(point) -> driver`, exact integers.
2. **The driver primitives**: the register/DMA/interrupt/poll operations,
   each with its own test.
3. **The self-test gate**: every driver proves itself before use; a
   failure refuses the driver (fail-closed).
4. **The boot ROMs**: the UEFI/BIOS/device-tree stubs that enumerate the
   hardware and hand it to the lift — one per firmware class, so the OS
   boots on any chipset.
5. **The three-anchor build**: the cross-compilation runs from the user
   space, the system space and the agent, and the tri-plane parity test
   proves the three agree on every target.
6. **The byte-identity proof**: the same input yields the same bytes on
   all nine targets — the omni-tautology's strongest claim.

## The honest boundary

Both pieces are **specified, not built**. The driver primitives exist (the
nine PCIe class drivers), and the cross-compilation targets exist (the
builders produce x86_64, arm64, the three desktops and wasm). What is not
yet built is the **lift/project for hardware** and the **three-anchor
build orchestration** — both bounded, both mechanical once the lattice is
the anchor. The honest claim: **the architecture makes them possible; the
implementation is the work remaining.**
