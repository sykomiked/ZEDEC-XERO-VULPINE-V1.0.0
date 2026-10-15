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
