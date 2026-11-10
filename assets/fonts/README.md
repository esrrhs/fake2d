# UI layout test font

`ui_layout_test.otf` is a renamed subset of Noto Sans CJK SC Regular
(SIL Open Font License 1.1; see OFL.txt), limited to the CJK sentences and
ellipsis used by the UI demo/regression tests. It is a deterministic,
small test fallback, not a general Chinese font. Install Noto CJK for full
language coverage. Original: https://github.com/notofonts/noto-cjk

Generated with fontTools.subset, retaining only the required codepoints;
family/PostScript names changed to Fake2D Layout Test.

Rebuild with `python3 tools/make-ui-test-font.py /path/to/NotoSansCJKsc-Regular.otf`
(requires fontTools). The script takes CJK codepoints from the shipped demo
and the UI regression sentence, preserves upstream font timestamps, and
renames the subset so it cannot be mistaken for a full Noto font.
