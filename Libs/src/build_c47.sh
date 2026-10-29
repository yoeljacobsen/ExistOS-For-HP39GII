#!/usr/bin/env bash
# =============================================================================
# build_c47.sh — Build Libs/libc47_core.a from C47 (https://gitlab.com/rpncalculators/c43)
# Target: ExistOS on HP 39gII (ARM926EJ-S, Thumb-interwork, DMCP5 single-file mode)
# =============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK_DIR="${C47_SRC_DIR:-/tmp/c43-build}"
OUT_LIB="${ROOT_DIR}/Libs/libc47_core.libc"

echo "==> Preparing C47 source tree in ${WORK_DIR}..."
if [[ ! -d "${WORK_DIR}/.git" ]]; then
    git clone --depth 1 https://gitlab.com/rpncalculators/c43.git "${WORK_DIR}"
    (
        cd "${WORK_DIR}"
        git submodule update --init --depth 1 dep/DMCP5_SDK dep/DMCP_SDK
    )
fi

GEN_DIR="${WORK_DIR}/build_gen"
OBJ_DIR="${WORK_DIR}/build_arm_objs"
mkdir -p "${GEN_DIR}" "${OBJ_DIR}"

echo "==> Building C47 host code generators (fonts, constants, catalogs)..."
PKG_FLAGS="$(pkg-config --cflags --libs freetype2)"

gcc -O2 -DGENERATE_CATALOGS \
    -I"${WORK_DIR}/src/c47" \
    -I"${WORK_DIR}/src/c47-gtk" \
    -I"${WORK_DIR}/src/generateCatalogs" \
    -I"${WORK_DIR}/dep/decNumberICU/include" \
    -I"${ROOT_DIR}/Libs/src/gmp/include" \
    "${WORK_DIR}/src/generateCatalogs/generateCatalogs.c" \
    "${WORK_DIR}/src/c47/items.c" \
    "${WORK_DIR}/src/c47/sort.c" \
    "${WORK_DIR}/src/c47/charString.c" \
    -o "${GEN_DIR}/generateCatalogs"

gcc -O2 \
    -I"${WORK_DIR}/src/c47" \
    -I"${WORK_DIR}/src/c47-gtk" \
    -I"${WORK_DIR}/src/ttf2RasterFonts" \
    -I"${WORK_DIR}/dep/decNumberICU/include" \
    -I"${ROOT_DIR}/Libs/src/gmp/include" \
    "${WORK_DIR}/src/ttf2RasterFonts/ttf2RasterFonts.c" \
    "${WORK_DIR}/src/c47/charString.c" \
    ${PKG_FLAGS} \
    -o "${GEN_DIR}/ttf2RasterFonts"

echo "==> Generating rasterFontsData.c..."
"${GEN_DIR}/ttf2RasterFonts" "${WORK_DIR}/res/fonts" "${GEN_DIR}/rasterFontsData.c"

gcc -O2 -DGENERATE_CONSTANTS \
    -I"${WORK_DIR}/src/c47" \
    -I"${WORK_DIR}/src/c47-gtk" \
    -I"${WORK_DIR}/src/generateConstants" \
    -I"${WORK_DIR}/dep/decNumberICU/include" \
    -I"${ROOT_DIR}/Libs/src/gmp/include" \
    "${WORK_DIR}/src/generateConstants/generateConstants.c" \
    "${GEN_DIR}/rasterFontsData.c" \
    "${WORK_DIR}/src/c47/fonts.c" \
    "${WORK_DIR}/src/c47/charString.c" \
    "${WORK_DIR}/dep/decNumberICU/source/decContext.c" \
    "${WORK_DIR}/dep/decNumberICU/source/decNumber.c" \
    -o "${GEN_DIR}/generateConstants"

echo "==> Generating constantPointers.[ch] and softmenuCatalogs.h..."
"${GEN_DIR}/generateConstants" "${GEN_DIR}/constantPointers.c" "${GEN_DIR}/constantPointers.h" "${GEN_DIR}/constantPointers2.c"
"${GEN_DIR}/generateCatalogs" "${GEN_DIR}/softmenuCatalogs.h"

COMMIT_SHA="$(git -C "${WORK_DIR}" rev-parse --short=8 HEAD)"
cat > "${GEN_DIR}/vcs.h" <<EOF
#ifndef VCS_H
#define VCS_H
#define VCS_COMMIT_ID "${COMMIT_SHA}"
#define GMP_VERSION_STRING "6.2.1"
#endif
EOF

# Install ExistOS-adapted DMCP headers
cp "${ROOT_DIR}/System/dmcp/dmcp.h"   "${GEN_DIR}/dmcp.h"
cp "${ROOT_DIR}/System/dmcp/ff_ifc.h" "${GEN_DIR}/ff_ifc.h"

# Ensure graphText.c local FatFs text helpers are static so they don't collide with System/filesystem/fatfs/ff.c
sed -i 's/^TCHAR\* f_gets/static TCHAR* f_gets/; s/^int f_putc /static int f_putc /; s/^int f_puts /static int f_puts /' \
    "${WORK_DIR}/src/c47/c47Extensions/graphText.c"

echo "==> Compiling C47 core for ARM926EJ-S..."
CFLAGS=(
    -mtune=arm926ej-s
    -mcpu=arm926ej-s
    -mlittle-endian
    -mfloat-abi=soft
    -mthumb-interwork
    -mthumb
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -Wno-unused-parameter
    -Wno-implicit-fallthrough
    '-D__weak=__attribute__((weak))'
    '-D__packed=__attribute__((__packed__))'
    -DDMCP_BUILD
    -DNEW_HW
    -DOS32BIT
    -DDECNUMBER_FASTMUL
    -I"${GEN_DIR}"
    -I"${WORK_DIR}/src/c47"
    -I"${WORK_DIR}/src/c47-dmcp5"
    -I"${WORK_DIR}/dep/decNumberICU/include"
    -I"${ROOT_DIR}/Libs/src/gmp/include"
)

rm -f "${OBJ_DIR}"/*.o

find "${WORK_DIR}/src/c47" -name "*.c" | sort | while read -r src; do
    rel="${src#${WORK_DIR}/src/c47/}"
    obj="${OBJ_DIR}/c47_${rel//\//_}.o"
    arm-none-eabi-gcc "${CFLAGS[@]}" -c "${src}" -o "${obj}"
done

for src in "${WORK_DIR}"/dep/decNumberICU/source/*.c; do
    base="$(basename "${src}")"
    arm-none-eabi-gcc "${CFLAGS[@]}" -c "${src}" -o "${OBJ_DIR}/dec_${base}.o"
done

for src in "${WORK_DIR}"/src/c47-dmcp5/hal/audio.c \
           "${WORK_DIR}"/src/c47-dmcp5/hal/io.c \
           "${WORK_DIR}"/src/c47-dmcp5/hal/print_ir.c; do
    base="$(basename "${src}")"
    arm-none-eabi-gcc "${CFLAGS[@]}" -c "${src}" -o "${OBJ_DIR}/hal_${base}.o"
done

for src in "${GEN_DIR}/rasterFontsData.c" "${GEN_DIR}/constantPointers.c"; do
    base="$(basename "${src}")"
    arm-none-eabi-gcc "${CFLAGS[@]}" -c "${src}" -o "${OBJ_DIR}/gen_${base}.o"
done

rm -f "${OUT_LIB}"
arm-none-eabi-ar rcs "${OUT_LIB}" "${OBJ_DIR}"/*.o

# Both core libraries define program_main(), the DMCP entry point, so they
# cannot be linked into one binary as-is. Rename this one; System/*/\*_app.c
# calls it by the new name.
arm-none-eabi-objcopy --redefine-sym program_main=c47_program_main "${OUT_LIB}"
echo "==> Successfully built ${OUT_LIB} ($(du -h "${OUT_LIB}" | cut -f1))"
