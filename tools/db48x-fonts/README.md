# DB48X help and menu fonts for the HP 39gII

DB48X draws a 400x240 screen; ExistOS shrinks it to the 39gII's 256x127 by
averaging 2x2 (or 2x1) blocks. DB48X's `HelpFont` (FogSans, 18 px, an outline
font whose 1-pixel strokes land at uneven positions) turns gray and fuzzy in
that shrink, and long help pages are hard to read.

The help page now uses its own fonts:

- `HelpTextFont.cc`: **PixelOperator at 16 px**, a pixel font whose outlines
  sit on the pixel grid, so strokes shrink to clean lines. Lines are 18 px, and
  about 55 characters fit on a line (FogSans: about 45).
- `HelpTextBoldFont.cc`: PixelOperator Bold, for the softkey menu labels and the
  white-on-black help styles (the selected link). Labels are short, so its one
  weakness at this size (m closing up towards n in running text) matters less,
  and its 2-pixel strokes hold up white on black. Menu rows are 18 px instead of
  20, so three rows give the stack 6 px back.

Glyphs PixelOperator lacks (arrows, Greek, math, super/subscripts: 728 of
them) come from FogSans, scaled to the same 9 px cap height, so coverage is the
same as DB48X's own font.

Stack level numbers and error messages keep the stock `HelpFont`. The Bold cut
was also tried for the whole help body; in running text its m closes up into an
n, so the body uses the regular cut.

White on black also needed a scaler change: the downscale leans partly covered
pixels towards ink, so thin black strokes survive, which turned white strokes
on black into dark gray. It now leans towards whichever colour is not the
background around the pixel (`c47_ink_background()` in
`System/dmcp/dmcp_existos.c`).

`Libs/src/build_db48x.sh` copies both `.cc` files into the DB48X tree and
patches `font.h`, `font.cc` (including `MenuFont`) and the help style table in
`user_interface.cc` to use them. A normal build needs nothing else.

## Regenerating

Needs Python fontTools and DB48X's `tools/ttf2font`, which builds against
FreeType (`libfreetype-dev`, or the headers from the matching FreeType release
plus the system `libfreetype.so.6`):

    F=/tmp/db48x-build/fonts
    python3 make_help_font.py $F/PixelOperator.ttf      $F/FogSans-ddd.ttf HelpText39.ttf
    python3 make_help_font.py $F/PixelOperator-Bold.ttf $F/FogSans-ddd.ttf HelpText39-Bold.ttf
    ttf2font -s 16 -S 120 -y 1 HelpTextFont     HelpText39.ttf      HelpTextFont.cc
    ttf2font -s 16 -S 120 -y 1 HelpTextBoldFont HelpText39-Bold.ttf HelpTextBoldFont.cc

`-S` is the line height in percent of the font's ascender-to-descender height
(15 px here, so 18 px lines; DB48X uses `-S 80` for its own fonts, which would
give cramped 12 px lines). `-y 1` moves the glyphs down a row to centre them.
Without `-t`, glyphs go through FreeType's monochrome rasteriser, as DB48X's own
fonts do. Built this way, `ttf2font` reproduces DB48X's shipped `HelpFont.cc`
byte for byte, so the tool is faithful.

## Licences

PixelOperator is © 2009-2015 Jayvee Enaguas (The Grandoplex Project), under the
SIL Open Font License 1.1. FogSans, shipped with DB48X, is Intel's Clear Sans
(© 2012 Intel Corporation) under the Apache License 2.0. The merged TTFs are
modified versions under the OFL: they are renamed HelpText39 and their name
tables keep both copyright notices.
