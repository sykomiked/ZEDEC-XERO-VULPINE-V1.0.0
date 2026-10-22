# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Top-level entry points. The kernel's own build and tests are kernel/Makefile
# (make -C kernel ...); the bare-metal kernels are build_system/Makefile.<arch>.

ARCHS ?= x86_64 arm64

.PHONY: ipfs-dist
# The release bucket, built twice from clean; fails unless both passes give the
# same Disc 1 SHA-256 and the same root CID (build_system/ipfs_dist.sh).
# Nothing is added to IPFS, uploaded or pinned.
ipfs-dist:
	bash build_system/ipfs_dist.sh $(ARCHS)

# The release layers L0 (hardware envelope), L1 (core boot engine: the boot
# files and core models) and L2 (the swarm), each built on the one before:
# PARENT_CID is the CID the previous step wrote (docs/LAYERS.md). Needs the
# boot files of ARCHS (build_system/baremetal_images.sh). The golden discs
# carry them with GOLDEN_LAYERS=$(LAYERS); `make ipfs-dist` does both.
LAYERS ?= build/layers
EFI_x86_64 ?= BOOTX64.EFI
EFI_arm64 ?= BOOTAA64.EFI
GOLDEN_MODELS ?= build_system/golden.models
GOLDEN_CORE ?= build_system/golden.core
GOLDEN_SWARM ?= build_system/golden.swarm
.PHONY: build-all-layers layers
build-all-layers:
	python3 build_system/mk_layers.py l0 --out $(LAYERS)
	PARENT_CID=$$(cat $(LAYERS)/L0.cid) && python3 build_system/mk_layers.py l1 --out $(LAYERS) \
	  --parent "$$PARENT_CID" $(foreach a,$(ARCHS),--boot $(a)=$(EFI_$(a))) \
	  --models $(GOLDEN_MODELS) --core $(GOLDEN_CORE)
	PARENT_CID=$$(cat $(LAYERS)/L1.cid) && python3 build_system/mk_layers.py l2 --out $(LAYERS) \
	  --parent "$$PARENT_CID" --models $(GOLDEN_MODELS) --swarm $(GOLDEN_SWARM)
	python3 build_system/mk_layers.py check $(LAYERS)/L0.json $(LAYERS)/L1.json $(LAYERS)/L2.json
layers: build-all-layers
