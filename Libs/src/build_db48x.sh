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

# Distribution arm-none-eabi packages often ship without libstdc++ headers
# (<cstdint> missing); prefer the full GNU Arm toolchain when it is installed.
ARM_TOOLCHAIN="${ARM_TOOLCHAIN:-${HOME}/.local/opt/gcc-arm-none-eabi-10.3-2021.10/bin}"
[[ -x "${ARM_TOOLCHAIN}/arm-none-eabi-g++" ]] && export PATH="${ARM_TOOLCHAIN}:${PATH}"

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

# Help text and softkey labels in a pixel font that stays crisp through the
# 400x240 -> 256x127 shrink, where the stock HelpFont blurs
# (tools/db48x-fonts/README.md). Help text takes the regular cut; white-on-black
# help styles (the selected link) and the menu take the bold one, because thin
# white strokes on black, as on the active softkey row, fade to gray in the
# shrink. Stack labels and errors keep HelpFont.
cp "${ROOT_DIR}/tools/db48x-fonts/HelpTextFont.cc"     "${WORK_DIR}/fonts/"
cp "${ROOT_DIR}/tools/db48x-fonts/HelpTextBoldFont.cc" "${WORK_DIR}/fonts/"
python3 - "${WORK_DIR}/src" <<'HELPFONTPATCH'
import sys
d = sys.argv[1]

def patch(name, pairs, marker):
    p = f"{d}/{name}"; s = open(p).read()
    if marker in s:
        return
    for old, new, count in pairs:
        assert s.count(old) == count, f"{name}: expected {count} of {old!r}"
        s = s.replace(old, new)
    open(p, "w").write(s)

patch("font.h", [("extern font_p HelpFont;\n",
                  "extern font_p HelpFont;\nextern font_p HelpTextFont;\n"
                  "extern font_p HelpTextBoldFont;\n", 1)], "HelpTextFont")
patch("font.cc", [
    ("font_p HelpFont;\n",
     "font_p HelpFont;\nfont_p HelpTextFont;\nfont_p HelpTextBoldFont;\n", 1),
    ("    GENERATED_FONT(HelpFont);\n",
     "    GENERATED_FONT(HelpFont);\n    GENERATED_FONT(HelpTextFont);\n"
     "    GENERATED_FONT(HelpTextBoldFont);\n", 1),
    ("    HelpBoldFont     = HelpFont;\n    HelpItalicFont   = HelpFont;\n",
     "    HelpBoldFont     = HelpTextFont;\n    HelpItalicFont   = HelpTextFont;\n", 1),
], "HelpTextFont")
patch("font.cc", [("    MenuFont         = HelpFont;\n",
                   "    MenuFont         = HelpTextBoldFont;\n", 1)],
      "MenuFont         = HelpTextBoldFont")
patch("user_interface.cc", [
    # The help style table
    ("        { HelpFont,         p::black,  p::white,",
     "        { HelpTextFont,     p::black,  p::white,", 1),
    ("        { HelpFont,         p::white,  p::black,",
     "        { HelpTextBoldFont, p::white,  p::black,", 1),
    ("        { HelpFont,         p::black,  p::gray50,",
     "        { HelpTextFont,     p::black,  p::gray50,", 1),
    ("        { HelpFont,         p::white,  p::gray10,",
     "        { HelpTextBoldFont, p::white,  p::gray10,", 1),
    # The "Keys:" line and subtitle command lists, and the scroll step
    ("font_p af = HelpFont;", "font_p af = HelpTextFont;", 1),
    ("font_p  af    = HelpFont;", "font_p  af    = HelpTextFont;", 1),
    ("    size height = HelpFont->height();\n",
     "    size height = HelpTextFont->height();\n", 1),
], "HelpTextFont")
HELPFONTPATCH

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

# blitter::surface assumes a word-aligned base ("Word-aligned address of
# surface buffer"), but a grob's bitmap sits right after its LEB128 type, width
# and height, at any byte offset, and so do font bitmaps. A Cortex-M4 performs
# unaligned word loads in hardware; the HP 39gII's ARM926 silently returns the
# word *rotated* instead. That is why rendered stack values -- drawn into a grob
# in the RPL arena and then blitted -- came out blank, and why the blit sometimes
# crashed on coordinates computed from rotated garbage. Round the base down to a
# word and carry the misalignment as a bit offset applied in the two helpers
# every word/bit lookup goes through. On a little-endian word, byte k holds bits
# 8k..8k+7, so k bytes off is 8k bits in.
python3 - "${WORK_DIR}/src/blitter.h" <<'BLITPATCH'
import sys
p = sys.argv[1]; s = open(p).read()
if "existos_origin" not in s:
    old_ctor = """            : pixels(p),
              w(w),"""
    new_ctor = """            : pixels((pixword *) ((uintptr_t) p & ~(uintptr_t) 3)),
              existos_origin(((uintptr_t) p & 3) * 8),
              w(w),"""
    assert old_ctor in s, "ctor"
    s = s.replace(old_ctor, new_ctor, 1)
    old_shift = """            return bitoffset % BPW;"""
    assert s.count(old_shift) == 1, "shift"
    s = s.replace(old_shift, """            return (bitoffset + existos_origin) % BPW;""")
    old_addr = """            return pixels + bitoffset / BPW;"""
    assert s.count(old_addr) == 1, "addr"
    s = s.replace(old_addr, """            return pixels + (bitoffset + existos_origin) / BPW;""")
    old_mem = """        pixword *pixels;        // Word-aligned address of surface buffer"""
    assert old_mem in s, "member"
    s = s.replace(old_mem, old_mem + """
        offset   existos_origin; // Bits from pixels to the real start (ExistOS)""")
    open(p, 'w').write(s)
BLITPATCH

# The DM42 firmware carries its own bitmap fonts (lib_mono, skr_mono, Free42),
# and DB48X reaches them through dmcp_font, which calls lcd_switchFont() and
# reads the line_font_t it selects. There are no such fonts here -- our
# lcd_switchFont() cannot hand back a real table -- so every glyph read from a
# dmcp_font dereferences garbage. The HeaderFont default above was one route in;
# draw_message() is another: it hard-codes LibMonoFont10x17, so the first error
# or progress box (e.g. "√" of a stack value, following a help link) faulted in
# dmcp_font::glyph. Substitute DB48X's own compiled-in fonts by size instead.
python3 - "${WORK_DIR}/src/font.cc" <<'FONTPATCH'
import sys
p = sys.argv[1]; s = open(p).read()
if "existos_dmcp_substitute" not in s:
    helper = """static font_p existos_dmcp_substitute(int index)
// ----------------------------------------------------------------------------
//   HP 39gII port: no DMCP fonts exist, so pick a DB48X font of similar size
// ----------------------------------------------------------------------------
{
    switch (index)
    {
    case 2: case 3: case 21:    return ReducedFont;   // lib_mono 12x20, 14x22, skr 18x24
    case 4: case 5:             return StackFont;     // lib_mono 17x25, 17x28
    default:                    return HelpFont;      // smaller lib_mono, Free42, skr 13x18
    }
}


"""
    old_h = "font::fuint dmcp_font::height()\n"
    old_g = "bool dmcp_font::glyph(unicode utf8cp, glyph_info &g) const\n"
    assert old_h in s and old_g in s
    s = s.replace(old_h, helper + old_h, 1)
    s = s.replace("""{
    // Switch to the correct DMCP font
    int fontnr = index();""", """{
    return existos_dmcp_substitute(index())->height();

    // Switch to the correct DMCP font
    int fontnr = index();""", 1)
    s = s.replace("""{
    // Map Unicode code points to corresonding entry in DMCP charset""", """{
    return existos_dmcp_substitute(index())->glyph(utf8cp, g);

    // Map Unicode code points to corresonding entry in DMCP charset""", 1)
    assert s.count("existos_dmcp_substitute(index())") == 2, "dmcp_font patch did not apply"
    open(p, "w").write(s)
FONTPATCH

# Decimal arithmetic builds its base-1000 digits ("kigits", uint16_t) in the
# scratchpad, which hands out bytes at any offset, then reads them back through
# kint pointers. On an odd address the ARM926 ignores bit 0 of a halfword
# access instead of faulting, so every such load and store hit the wrong
# halfword: "1.5" ENTER showed 0., 1.5/3 gave 7.705..., and the Newton loop in
# sqrt never converged, so the calculator hung. A typedef may lower alignment;
# with aligned(1) GCC emits byte accesses for every kint dereference.
sed -i 's|^    using kint = uint16_t;|    typedef uint16_t kint __attribute__((aligned(1)));|' "${WORK_DIR}/src/decimal.h"
grep -q "typedef uint16_t kint __attribute__((aligned(1)))" "${WORK_DIR}/src/decimal.h" \
    || { echo "ERROR: kint alignment patch did not apply"; exit 1; }
grep -q "existos_origin" "${WORK_DIR}/src/blitter.h" || { echo "ERROR: alignment patch did not apply"; exit 1; }

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
    fonts/HelpTextFont.cc
    fonts/HelpTextBoldFont.cc
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

# DB48X reads its help files one f_read per character, and every f_read takes
# and releases the volume lock, which costs several seconds per help topic
# lookup (and a long press on a key is one). f_read_cached (System/filesystem/
# fatfs/ff.c) serves reads inside the already-cached sector without the lock.
arm-none-eabi-objcopy --redefine-sym f_read=f_read_cached "${OUT_LIB}"

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
# Online help is not compiled in: DB48X reads HELPFILE_NAME and a byte-offset
# index HELPINDEX_NAME from the filesystem at runtime. Produce both, exactly as
# DB48X's Makefile does, so they can be copied to /help/ on the calculator.
HELP_OUT="${ROOT_DIR}/build/db48x-help"
mkdir -p "${HELP_OUT}"
cp "${WORK_DIR}/help/db48x.md" "${HELP_OUT}/db48x.md"
grep -b '^#\|^\* `[^`]*`' "${HELP_OUT}/db48x.md" \
    | sed -e 's/:\(\* `[^`]*`\).*/:\1/g' | LC_ALL=C sort -k2 -t: > "${HELP_OUT}/db48x.idx"
echo "==> Help files for /help/ on the calculator: ${HELP_OUT}/db48x.{md,idx}"

echo "==> Successfully built ${OUT_LIB} ($(du -h "${OUT_LIB}" | cut -f1))"
