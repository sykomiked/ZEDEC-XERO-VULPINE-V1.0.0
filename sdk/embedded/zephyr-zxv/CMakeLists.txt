# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# The ZXV Zephyr module: builds the libzxv subset straight from
# sdk/libzxv/src and kernel/src (same list as ../sources.mk; checked by
# ../tools/check_lists.py), the port layer and the zxe helpers.

if(CONFIG_ZXV)

get_filename_component(ZXE_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
set(ZXE_LIBZXV ${ZXE_ROOT}/../libzxv)
set(ZXE_K ${ZXE_ROOT}/../../kernel)

# zxe-sources-begin (kept equal to sources.mk by tools/check_lists.py)
set(ZXV_LIBZXV_SRCS
  ${ZXE_LIBZXV}/src/zxv_api.c
  ${ZXE_LIBZXV}/src/zxv_core.c
  ${ZXE_LIBZXV}/src/zxv_econ.c
  ${ZXE_LIBZXV}/src/zxv_crypto.c
  ${ZXE_LIBZXV}/src/zxv_tensor.c
  ${ZXE_LIBZXV}/src/zxv_evm.c
  ${ZXE_LIBZXV}/src/zxv_hex.c
)
set(ZXV_KERNEL_SRCS
  ${ZXE_K}/src/iso20022/iso20022.c
  ${ZXE_K}/src/finance/triple_ledger.c
  ${ZXE_K}/src/rational/rational.c
  ${ZXE_K}/src/lpres/lpres.c
  ${ZXE_K}/src/lpres/lpres_hex.c
  ${ZXE_K}/src/rmag/rmag.c
  ${ZXE_K}/src/phase_coord/phase_coord.c
  ${ZXE_K}/src/harmonic/zt_harmonic_wire.c
  ${ZXE_K}/src/swarm/swarm_budget.c
  ${ZXE_K}/src/swarm/swarm_emotion.c
  ${ZXE_K}/src/mlkem/keccak.c
  ${ZXE_K}/src/robin_debanks/sha256.c
)
# zxe-sources-end

set(ZXV_KERNEL_INCS iso20022 finance vino_stores vino surplus edp_risk onepolicy rational
    lpres event_space rmag phase_coord harmonic swarm mlkem e8 modbind trispace)

if(CONFIG_ZXV_PQC)
  file(GLOB ZXV_ED25519_SRCS ${ZXE_K}/third_party/ed25519/src/*.c)
  list(APPEND ZXV_KERNEL_SRCS
    ${ZXE_K}/src/pqsec/pq_mlkem1024.c
    ${ZXE_K}/src/pqsec/pq_mldsa87.c
    ${ZXE_K}/src/pqsec/mldsa/fips202.c
    ${ZXV_ED25519_SRCS})
endif()

zephyr_library_named(zxv)

set(ZXV_ALL_SRCS ${ZXV_LIBZXV_SRCS} ${ZXV_KERNEL_SRCS})
zephyr_library_sources(
  ${ZXV_ALL_SRCS}
  ${ZXE_ROOT}/port/src/zxp_core.c
  ${ZXE_ROOT}/port/src/zxp_zephyr.c
  ${ZXE_ROOT}/zxe/src/zxe_ledger.c
  ${ZXE_ROOT}/zxe/src/zxe_clock.c
)

# Public: what an application includes.
zephyr_include_directories(${ZXE_ROOT}/port/include ${ZXE_ROOT}/zxe/include ${ZXE_LIBZXV}/include)

# Private to the library: the compat pthread.h first, then libzxv and the
# kernel module headers.
zephyr_library_include_directories(BEFORE ${ZXE_ROOT}/port/compat)
zephyr_library_include_directories(${ZXE_LIBZXV}/src ${ZXE_K}/include)
foreach(d ${ZXV_KERNEL_INCS})
  zephyr_library_include_directories(${ZXE_K}/src/${d})
endforeach()
if(CONFIG_ZXV_PQC)
  zephyr_library_include_directories(${ZXE_K}/src/pqsec ${ZXE_K}/third_party/ed25519/src)
endif()

# See ../sources.mk for each flag: no engine, getentropy routed to the
# port, the exact (hosted) ledger mirror, kernel bring-up records not
# retained.
zephyr_library_compile_definitions(ZXV_NO_TENSOR ZXV_RANDOM_GETENTROPY TEST_HOST retain=used)
if(NOT CONFIG_ZXV_PQC)
  zephyr_library_compile_definitions(ZXV_NO_PQC)
endif()
if(NOT CONFIG_THREAD_LOCAL_STORAGE)
  # No per-thread storage: one owner thread makes every ZXV call (zxp.h).
  zephyr_library_compile_definitions(ZXP_NO_TLS)
endif()

# Only libzxv and kernel sources get the port prelude (malloc, pthread and
# getentropy mapped to the port) and keep their own warning policy.
set_source_files_properties(${ZXV_ALL_SRCS} PROPERTIES
  COMPILE_OPTIONS "-include;zxp_prelude.h;-w;-Werror=implicit-function-declaration")

endif()
