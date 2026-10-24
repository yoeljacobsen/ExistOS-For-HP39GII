#!/usr/bin/env bash
# =============================================================================
# build_db48x.sh — Build Libs/libdb48x_core.libc from DB48X
#                  (https://github.com/c3d/db48x)
# Target: ExistOS on HP 39gII (ARM926EJ-S, Thumb-interwork)
#
# DB48X targets the SwissMicros DM42 through DMCP, the same platform API this
# repo already implements for C47 (System/c47/dmcp.h + the ExistOS glue). We do
# not build a DMCP .pgm: the sources are compiled into a static library that
# System/db48x links against, so the SDK's startup and syscall stubs are
# deliberately excluded.
# =============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK_DIR="${DB48X_SRC_DIR:-/tmp/db48x-build}"
OUT_LIB="${ROOT_DIR}/Libs/libdb48x_core.libc"

echo "==> Preparing DB48X source tree in ${WORK_DIR}..."
if [[ ! -d "${WORK_DIR}/.git" ]]; then
    git clone --depth 1 https://github.com/c3d/db48x.git "${WORK_DIR}"
fi

GEN_DIR="${WORK_DIR}/build_existos"
OBJ_DIR="${WORK_DIR}/build_arm_objs"
mkdir -p "${GEN_DIR}" "${OBJ_DIR}"

# ExistOS-adapted DMCP headers, shared with C47.
cp "${ROOT_DIR}/System/c47/dmcp.h"   "${GEN_DIR}/dmcp.h"
cp "${ROOT_DIR}/System/c47/ff_ifc.h" "${GEN_DIR}/ff_ifc.h"

VERSION="$(git -C "${WORK_DIR}" describe --tags --always 2>/dev/null || echo existos)"

CXXFLAGS=(
    -mtune=arm926ej-s
    -mcpu=arm926ej-s
    -mlittle-endian
    -mfloat-abi=soft
    -mthumb-interwork
    -mthumb
    -Os
    -std=gnu++17   # DB48X uses typeof(), a GNU extension, in its SETTING macro
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -Wno-unused-parameter
    -Wno-implicit-fallthrough
    -DNDEBUG
    -DDM42
    -DMEMORY=100
    # DMCP headers use the toolchain's __packed/__weak spellings
    '-D__packed=__attribute__((__packed__))'
    '-D__weak=__attribute__((weak))'
    # Normally set by DB48X's Makefile from the firmware NAME
    '-DHELPFILE_NAME="/help/db48x.md"'
    '-DHELPINDEX_NAME="/help/db48x.idx"'
    "-DDB48X_VERSION=\"${VERSION}\""
    -I"${GEN_DIR}"
    -I"${WORK_DIR}/src"
    -I"${WORK_DIR}/src/dmcp"
    -I"${WORK_DIR}/src/dm42"   # MODEL-specific headers (main.h, qspi_crc.h)
    -I"${WORK_DIR}/recorder"
)

SOURCES=(
    src/dmcp/target.cc
    src/dmcp/sysmenu.cc
    src/dmcp/main.cc
    fonts/EditorFont.cc
    fonts/HelpFont.cc
    fonts/ReducedFont.cc
    fonts/StackFont.cc
)
while IFS= read -r f; do SOURCES+=("src/$(basename "$f")"); done < <(ls "${WORK_DIR}"/src/*.cc | grep -v "/tests\.cc$")

rm -f "${OBJ_DIR}"/*.o
fail=0
for rel in "${SOURCES[@]}"; do
    src="${WORK_DIR}/${rel}"
    [[ -f "${src}" ]] || { echo "   (skip missing ${rel})"; continue; }
    obj="${OBJ_DIR}/$(echo "${rel}" | tr '/' '_').o"
    if ! arm-none-eabi-g++ "${CXXFLAGS[@]}" -c "${src}" -o "${obj}" 2> "${obj}.log"; then
        echo "   FAILED ${rel}"
        head -5 "${obj}.log"
        fail=$((fail + 1))
    fi
done

if (( fail )); then
    echo "==> ${fail} source file(s) failed to compile; see ${OBJ_DIR}/*.log"
    exit 1
fi

rm -f "${OUT_LIB}"
arm-none-eabi-ar rcs "${OUT_LIB}" "${OBJ_DIR}"/*.o
echo "==> Successfully built ${OUT_LIB} ($(du -h "${OUT_LIB}" | cut -f1))"
