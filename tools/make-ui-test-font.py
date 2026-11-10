#!/usr/bin/env python3
"""Regenerate the tiny renamed CJK UI fixture; requires fonttools.

Usage: python3 tools/make-ui-test-font.py /path/to/NotoSansCJKsc-Regular.otf
Upstream: https://github.com/notofonts/noto-cjk/tree/main/Sans/OTF/SimplifiedChinese
License: assets/fonts/OFL.txt. Do not ship the full upstream font in this repo.
"""
from pathlib import Path
import re
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parent.parent
text = (root / "scripts/ui_demo.lua").read_text()
text += "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事…"
characters = "".join(re.findall(r"[\u3000-\u9fff…]+", text)) + " /1234567890+%()"
font = TTFont(sys.argv[1], recalcTimestamp=False)
options = subset.Options()
options.name_IDs = ["*"]
options.name_legacy = True
options.name_languages = ["*"]
subsetter = subset.Subsetter(options=options)
subsetter.populate(text=characters)
subsetter.subset(font)
for name in font["name"].names:
    if name.nameID in (1, 2, 3, 4, 6, 16, 17):
        value = ("Regular" if name.nameID in (2, 17) else
                 "Fake2DLayoutTest-Regular" if name.nameID == 6 else
                 "Fake2D Layout Test")
        name.string = value.encode(name.getEncoding())
font.save(root / "assets/fonts/ui_layout_test.otf")
