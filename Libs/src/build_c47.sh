 build_c47.sh — Build Libs/libc47_core.a from C47#!/usr/bin/env bash
 build_c47.sh — Build Libs/libc47_core.a from C47# =============================================================================
 build_c47.sh — Build Libs/libc47_core.a from C47# build_c47.sh — Build Libs/libc47_core.a from C47 (https://gitlab.com/rpncalculators/c43)
 build_c47.sh — Build Libs/libc47_core.a from C47# Target: ExistOS on HP 39gII (ARM926EJ-S, Thumb-interwork, DMCP5 single-file mode)
 build_c47.sh — Build Libs/libc47_core.a from C47# =============================================================================
 build_c47.sh — Build Libs/libc47_core.a from C47set -euo pipefail
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
 build_c47.sh — Build Libs/libc47_core.a from C47WORK_DIR="${C47_SRC_DIR:-/tmp/c43-build}"
 build_c47.sh — Build Libs/libc47_core.a from C47OUT_LIB="${ROOT_DIR}/Libs/libc47_core.libc"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47echo "==> Preparing C47 source tree in ${WORK_DIR}..."
 build_c47.sh — Build Libs/libc47_core.a from C47if [[ ! -d "${WORK_DIR}/.git" ]]; then
 build_c47.sh — Build Libs/libc47_core.a from C47    git clone --depth 1 https://gitlab.com/rpncalculators/c43.git "${WORK_DIR}"
 build_c47.sh — Build Libs/libc47_core.a from C47    (
 build_c47.sh — Build Libs/libc47_core.a from C47        cd "${WORK_DIR}"
 build_c47.sh — Build Libs/libc47_core.a from C47        git submodule update --init --depth 1 dep/DMCP5_SDK dep/DMCP_SDK
 build_c47.sh — Build Libs/libc47_core.a from C47    )
 build_c47.sh — Build Libs/libc47_core.a from C47fi
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47GEN_DIR="${WORK_DIR}/build_gen"
 build_c47.sh — Build Libs/libc47_core.a from C47OBJ_DIR="${WORK_DIR}/build_arm_objs"
 build_c47.sh — Build Libs/libc47_core.a from C47mkdir -p "${GEN_DIR}" "${OBJ_DIR}"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47echo "==> Building C47 host code generators (fonts, constants, catalogs)..."
 build_c47.sh — Build Libs/libc47_core.a from C47PKG_FLAGS="$(pkg-config --cflags --libs freetype2)"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47gcc -O2 -DGENERATE_CATALOGS \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/c47" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/c47-gtk" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/generateCatalogs" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/dep/decNumberICU/include" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${ROOT_DIR}/Libs/src/gmp/include" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/generateCatalogs/generateCatalogs.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/c47/items.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/c47/sort.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/c47/charString.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -o "${GEN_DIR}/generateCatalogs"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47gcc -O2 \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/c47" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/c47-gtk" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/ttf2RasterFonts" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/dep/decNumberICU/include" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${ROOT_DIR}/Libs/src/gmp/include" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/ttf2RasterFonts/ttf2RasterFonts.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/c47/charString.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    ${PKG_FLAGS} \
 build_c47.sh — Build Libs/libc47_core.a from C47    -o "${GEN_DIR}/ttf2RasterFonts"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47echo "==> Generating rasterFontsData.c..."
 build_c47.sh — Build Libs/libc47_core.a from C47"${GEN_DIR}/ttf2RasterFonts" "${WORK_DIR}/res/fonts" "${GEN_DIR}/rasterFontsData.c"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47gcc -O2 -DGENERATE_CONSTANTS \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/c47" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/c47-gtk" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/generateConstants" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/dep/decNumberICU/include" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${ROOT_DIR}/Libs/src/gmp/include" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/generateConstants/generateConstants.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${GEN_DIR}/rasterFontsData.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/c47/fonts.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/c47/charString.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/dep/decNumberICU/source/decContext.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/dep/decNumberICU/source/decNumber.c" \
 build_c47.sh — Build Libs/libc47_core.a from C47    -o "${GEN_DIR}/generateConstants"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47echo "==> Generating constantPointers.[ch] and softmenuCatalogs.h..."
 build_c47.sh — Build Libs/libc47_core.a from C47"${GEN_DIR}/generateConstants" "${GEN_DIR}/constantPointers.c" "${GEN_DIR}/constantPointers.h" "${GEN_DIR}/constantPointers2.c"
 build_c47.sh — Build Libs/libc47_core.a from C47"${GEN_DIR}/generateCatalogs" "${GEN_DIR}/softmenuCatalogs.h"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47COMMIT_SHA="$(git -C "${WORK_DIR}" rev-parse --short=8 HEAD)"
 build_c47.sh — Build Libs/libc47_core.a from C47cat > "${GEN_DIR}/vcs.h" <<EOF
 build_c47.sh — Build Libs/libc47_core.a from C47#ifndef VCS_H
 build_c47.sh — Build Libs/libc47_core.a from C47#define VCS_H
 build_c47.sh — Build Libs/libc47_core.a from C47#define VCS_COMMIT_ID "${COMMIT_SHA}"
 build_c47.sh — Build Libs/libc47_core.a from C47#define GMP_VERSION_STRING "6.2.1"
 build_c47.sh — Build Libs/libc47_core.a from C47#endif
 build_c47.sh — Build Libs/libc47_core.a from C47EOF
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47# Install ExistOS-adapted DMCP headers
 build_c47.sh — Build Libs/libc47_core.a from C47cp "${ROOT_DIR}/System/c47/dmcp.h"   "${GEN_DIR}/dmcp.h"
 build_c47.sh — Build Libs/libc47_core.a from C47cp "${ROOT_DIR}/System/c47/ff_ifc.h" "${GEN_DIR}/ff_ifc.h"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47# Ensure graphText.c local FatFs text helpers are static so they don't collide with System/filesystem/fatfs/ff.c
 build_c47.sh — Build Libs/libc47_core.a from C47sed -i 's/^TCHAR\* f_gets/static TCHAR* f_gets/; s/^int f_putc /static int f_putc /; s/^int f_puts /static int f_puts /' \
 build_c47.sh — Build Libs/libc47_core.a from C47    "${WORK_DIR}/src/c47/c47Extensions/graphText.c"
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47echo "==> Compiling C47 core for ARM926EJ-S..."
 build_c47.sh — Build Libs/libc47_core.a from C47CFLAGS=(
 build_c47.sh — Build Libs/libc47_core.a from C47    -mtune=arm926ej-s
 build_c47.sh — Build Libs/libc47_core.a from C47    -mcpu=arm926ej-s
 build_c47.sh — Build Libs/libc47_core.a from C47    -mlittle-endian
 build_c47.sh — Build Libs/libc47_core.a from C47    -mfloat-abi=soft
 build_c47.sh — Build Libs/libc47_core.a from C47    -mthumb-interwork
 build_c47.sh — Build Libs/libc47_core.a from C47    -mthumb
 build_c47.sh — Build Libs/libc47_core.a from C47    -Os
 build_c47.sh — Build Libs/libc47_core.a from C47    -ffunction-sections
 build_c47.sh — Build Libs/libc47_core.a from C47    -fdata-sections
 build_c47.sh — Build Libs/libc47_core.a from C47    -fno-exceptions
 build_c47.sh — Build Libs/libc47_core.a from C47    -fno-unwind-tables
 build_c47.sh — Build Libs/libc47_core.a from C47    -fno-asynchronous-unwind-tables
 build_c47.sh — Build Libs/libc47_core.a from C47    -Wno-unused-parameter
 build_c47.sh — Build Libs/libc47_core.a from C47    -Wno-implicit-fallthrough
 build_c47.sh — Build Libs/libc47_core.a from C47    '-D__weak=__attribute__((weak))'
 build_c47.sh — Build Libs/libc47_core.a from C47    '-D__packed=__attribute__((__packed__))'
 build_c47.sh — Build Libs/libc47_core.a from C47    -DDMCP_BUILD
 build_c47.sh — Build Libs/libc47_core.a from C47    -DNEW_HW
 build_c47.sh — Build Libs/libc47_core.a from C47    -DOS32BIT
 build_c47.sh — Build Libs/libc47_core.a from C47    -DDECNUMBER_FASTMUL
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${GEN_DIR}"
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/c47"
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/src/c47-dmcp5"
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${WORK_DIR}/dep/decNumberICU/include"
 build_c47.sh — Build Libs/libc47_core.a from C47    -I"${ROOT_DIR}/Libs/src/gmp/include"
 build_c47.sh — Build Libs/libc47_core.a from C47)
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47rm -f "${OBJ_DIR}"/*.o
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47find "${WORK_DIR}/src/c47" -name "*.c" | sort | while read -r src; do
 build_c47.sh — Build Libs/libc47_core.a from C47    rel="${src#${WORK_DIR}/src/c47/}"
 build_c47.sh — Build Libs/libc47_core.a from C47    obj="${OBJ_DIR}/c47_${rel//\//_}.o"
 build_c47.sh — Build Libs/libc47_core.a from C47    arm-none-eabi-gcc "${CFLAGS[@]}" -c "${src}" -o "${obj}"
 build_c47.sh — Build Libs/libc47_core.a from C47done
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47for src in "${WORK_DIR}"/dep/decNumberICU/source/*.c; do
 build_c47.sh — Build Libs/libc47_core.a from C47    base="$(basename "${src}")"
 build_c47.sh — Build Libs/libc47_core.a from C47    arm-none-eabi-gcc "${CFLAGS[@]}" -c "${src}" -o "${OBJ_DIR}/dec_${base}.o"
 build_c47.sh — Build Libs/libc47_core.a from C47done
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47for src in "${WORK_DIR}"/src/c47-dmcp5/hal/audio.c \
 build_c47.sh — Build Libs/libc47_core.a from C47           "${WORK_DIR}"/src/c47-dmcp5/hal/io.c \
 build_c47.sh — Build Libs/libc47_core.a from C47           "${WORK_DIR}"/src/c47-dmcp5/hal/print_ir.c; do
 build_c47.sh — Build Libs/libc47_core.a from C47    base="$(basename "${src}")"
 build_c47.sh — Build Libs/libc47_core.a from C47    arm-none-eabi-gcc "${CFLAGS[@]}" -c "${src}" -o "${OBJ_DIR}/hal_${base}.o"
 build_c47.sh — Build Libs/libc47_core.a from C47done
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47for src in "${GEN_DIR}/rasterFontsData.c" "${GEN_DIR}/constantPointers.c"; do
 build_c47.sh — Build Libs/libc47_core.a from C47    base="$(basename "${src}")"
 build_c47.sh — Build Libs/libc47_core.a from C47    arm-none-eabi-gcc "${CFLAGS[@]}" -c "${src}" -o "${OBJ_DIR}/gen_${base}.o"
 build_c47.sh — Build Libs/libc47_core.a from C47done
 build_c47.sh — Build Libs/libc47_core.a from C47
 build_c47.sh — Build Libs/libc47_core.a from C47rm -f "${OUT_LIB}"
 build_c47.sh — Build Libs/libc47_core.a from C47arm-none-eabi-ar rcs "${OUT_LIB}" "${OBJ_DIR}"/*.o
 build_c47.sh — Build Libs/libc47_core.a from C47echo "==> Successfully built ${OUT_LIB} ($(du -h "${OUT_LIB}" | cut -f1))"
