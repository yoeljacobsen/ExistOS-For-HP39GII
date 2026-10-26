#!/usr/bin/env python3
"""Build a TTF for DB48X's help text on the HP 39gII.

DB48X draws on a 400x240 screen that ExistOS shrinks to 256x127. Its stock
HelpFont (FogSans at 18 px) has 1-pixel strokes at uneven positions, and the
shrink turns them to gray mush. PixelOperator at 16 px is a pixel font: its
outlines sit on the pixel grid, so strokes shrink to clean lines.

PixelOperator only covers Latin-1, though, and DB48X's help uses arrows,
Greek, math operators, super/subscripts and so on. Glyphs it lacks are taken
from FogSans (the font DB48X uses for everything), scaled so their cap height
matches PixelOperator's.

Usage: make_help_font.py <PixelOperator[-Bold].ttf> <FogSans.ttf> <out.ttf>
Then:  ttf2font -s 16 -S 120 -y 1 HelpTextFont <out.ttf> HelpTextFont.cc
(see tools/db48x-fonts/README.md)
"""

import sys

from fontTools.pens.transformPen import TransformPen
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable
from fontTools.ttLib.tables import ttProgram

# Pixel sizes at which the two fonts have the same cap height (9 px):
# PixelOperator at 16, FogSans at 12.
PIXEL_SIZE = 16
FALLBACK_EQUIVALENT_SIZE = 12


def main(pixel_path, fallback_path, out_path):
    base = TTFont(pixel_path)
    fall = TTFont(fallback_path)

    base_cmap = dict(base.getBestCmap())
    fall_cmap = fall.getBestCmap()
    fall_glyphs = fall.getGlyphSet()

    scale = (base['head'].unitsPerEm / fall['head'].unitsPerEm) \
        * (FALLBACK_EQUIVALENT_SIZE / PIXEL_SIZE)

    glyf = base['glyf']
    hmtx = base['hmtx']
    order = list(base.getGlyphOrder())
    added = {}

    for cp in sorted(fall_cmap):
        if cp in base_cmap or cp < 0x20:
            continue
        src = fall_cmap[cp]
        name = 'fb_' + src
        if name not in added:
            rec = DecomposingRecordingPen(fall_glyphs)
            fall_glyphs[src].draw(rec)
            pen = TTGlyphPen(None)
            rec.replay(TransformPen(pen, (scale, 0, 0, scale, 0, 0)))
            glyf[name] = pen.glyph()
            adv = round(fall['hmtx'][src][0] * scale)
            g = glyf[name]
            g.recalcBounds(glyf)
            hmtx[name] = (adv, getattr(g, 'xMin', 0))
            order.append(name)
            added[name] = True
        for table in base['cmap'].tables:
            if table.isUnicode() and (cp <= 0xFFFF or table.format == 12):
                table.cmap[cp] = name

    # Code points above the BMP need a format 12 subtable.
    if not any(t.format == 12 for t in base['cmap'].tables):
        t = CmapSubtable.newSubtable(12)
        t.platformID, t.platEncID, t.language = 3, 10, 0
        t.cmap = {}
        for sub in base['cmap'].tables:
            if sub.isUnicode():
                t.cmap.update(sub.cmap)
        for cp in sorted(fall_cmap):
            if cp > 0xFFFF and cp not in base_cmap:
                t.cmap[cp] = 'fb_' + fall_cmap[cp]
        base['cmap'].tables.append(t)

    base.setGlyphOrder(order)
    base['maxp'].numGlyphs = len(order)
    # Instructions for PixelOperator's own glyphs would refer to a glyph count
    # and point numbers we no longer match; the font is a bitmap design at
    # integer sizes, so hinting adds nothing here.
    for tag in ('fpgm', 'prep', 'cvt ', 'hdmx', 'LTSH', 'VDMX'):
        if tag in base:
            del base[tag]
    for name in order:
        g = glyf[name]
        if hasattr(g, 'program'):
            g.program = ttProgram.Program()
            g.program.fromBytecode(b'')
    # A modified font under the OFL takes a name of its own and keeps the
    # original copyright and licence; the FogSans glyphs are Apache 2.0.
    name = base['name']
    style = name.getDebugName(2) or 'Regular'
    full = 'HelpText39' + ('' if style == 'Regular' else ' ' + style)
    for nid, val in ((1, 'HelpText39'), (3, full), (4, full),
                     (6, full.replace(' ', '-'))):
        name.setName(val, nid, 3, 1, 0x409)
        name.setName(val, nid, 1, 0, 0)
    for rec in list(name.names):
        if rec.nameID == 0:
            name.setName(rec.toUnicode() + ' Fallback glyphs from Fog Sans: '
                         + fall['name'].getDebugName(0),
                         0, rec.platformID, rec.platEncID, rec.langID)
    base.save(out_path)
    print(f'{out_path}: {len(base_cmap)} pixel glyphs, '
          f'{len(added)} fallback glyphs at scale {scale:.3f}')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
