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
cp "${ROOT_DIR}/System/dmcp/dmcp.h"   "${GEN_DIR}/dmcp.h"
cp "${ROOT_DIR}/System/dmcp/ff_ifc.h" "${GEN_DIR}/ff_ifc.h"

VERSION="$(git -C "${WORK_DIR}" describe --tags --always 2>/dev/null || echo existos)"

# program_init() allocates the RPL runtime arena with malloc() and never frees
# it, and it runs on every launch -- so under ExistOS, where the application is
# started and exited repeatedly rather than being the only program on the
# device, the second launch leaked the arena and failed. Hand it a reusable one
# from the platform layer instead. (build_c47.sh patches its upstream the same
# way, for the same kind of reason.)
# The declaration goes at namespace scope: a linkage specification is not
# allowed inside a function body.
sed -i 's|^surface Screen(|extern "C" void *dmcp_rpl_arena(size_t size);\n\nsurface Screen(|' \
    "${WORK_DIR}/src/dmcp/main.cc"
sed -i 's|byte \*memory = (byte \*) malloc(size);|byte *memory = (byte *) dmcp_rpl_arena(size);|' \
    "${WORK_DIR}/src/dmcp/main.cc"
grep -q "dmcp_rpl_arena" "${WORK_DIR}/src/dmcp/main.cc" || { echo "ERROR: arena patch did not apply"; exit 1; }

# HeaderFont defaults to LIB17, one of the DM42 firmware's own bitmap fonts.
# Those live in DMCP and have no counterpart here, so every metric taken from
# them is garbage: the header reported a height of roughly the whole screen,
# which left no room for the stack or the menu and put the battery annunciator
# in the middle of the display. Every other font setting already defaults to one
# of DB48X's own compiled-in fonts; make this one match.
sed -i 's|\(SETTING_BITS(HeaderFont.*\)settings::LIB17|\1settings::REDUCED|' \
    "${WORK_DIR}/src/ids.tbl"
grep -q "SETTING_BITS(HeaderFont.*settings::REDUCED" "${WORK_DIR}/src/ids.tbl" \
    || { echo "ERROR: header font patch did not apply"; exit 1; }

# Expose the bits the ExistOS port needs from DB48X's UI, with C linkage. The
# HP 39gII has a hardware annunciator row outside the 256x127 bitmap, so shift
# and alpha can be shown there for free instead of spending screen rows on
# DB48X's own header; stack_screen_top() says where that header ends, so the
# port can drop it and give the space to the stack.
if ! grep -q "db48x_ui_state" "${WORK_DIR}/src/dmcp/main.cc"; then
    cat >> "${WORK_DIR}/src/dmcp/main.cc" <<'ACCESSOR'

// ---- ExistOS port accessors (added by Libs/src/build_db48x.sh) -------------
extern "C" int db48x_ui_state(int *stack_top)
{
    if (stack_top)
        *stack_top = ui.stack_screen_top();
    return (int) ((ui.shift_plane() & 3) | ((ui.alpha_plane() & 3) << 2));
}

extern "C" void db48x_ui_geometry(int *top, int *bottom, int *menu, int *depth)
{
    if (top)    *top    = ui.stack_screen_top();
    if (bottom) *bottom = ui.stack_screen_bottom();
    if (menu)   *menu   = ui.menu_screen_bottom();
    if (depth)  *depth  = (int) rt.depth();
}
ACCESSOR
fi

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

# Both core libraries define program_main(), the DMCP entry point, so they
# cannot be linked into one binary as-is. Rename this one; System/*/\*_app.c
# calls it by the new name.
arm-none-eabi-objcopy --redefine-sym program_main=db48x_program_main "${OUT_LIB}"

# DB48X and C47 are linked into one binary and both define a few globals under
# the same name -- they were each written to be the only program on a DM42.
# Rename DB48X's. --redefine-sym rewrites references inside the archive too, so
# the library stays self-consistent.
#
# Recompute rather than hard-code: if a future DB48X or C47 adds another clash,
# this reports it instead of failing at link time with a bare symbol name.
C47_LIB="${ROOT_DIR}/Libs/libc47_core.libc"
if [[ -f "${C47_LIB}" ]]; then
    mapfile -t COLLISIONS < <(comm -12 \
        <(arm-none-eabi-nm --defined-only "${C47_LIB}" | awk 'NF==3 && $2 ~ /[A-TV-Z]/ {print $3}' | sort -u) \
        <(arm-none-eabi-nm --defined-only "${OUT_LIB}" | awk 'NF==3 && $2 ~ /[A-TV-Z]/ {print $3}' | sort -u))
    if (( ${#COLLISIONS[@]} )); then
        echo "==> Renaming ${#COLLISIONS[@]} symbol(s) that also exist in libc47_core:"
        for sym in "${COLLISIONS[@]}"; do
            echo "       ${sym} -> db48x_${sym}"
            arm-none-eabi-objcopy --redefine-sym "${sym}=db48x_${sym}" "${OUT_LIB}"
        done
    fi
fi
echo "==> Successfully built ${OUT_LIB} ($(du -h "${OUT_LIB}" | cut -f1))"
