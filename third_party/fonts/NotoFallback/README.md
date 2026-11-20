# Noto menu glyph subsets

These compact fonts provide the Japanese, Korean, Chinese, Cyrillic, Latin and Arabic glyphs used by the
translated in-game overlay and launcher. They are embedded by
`runtime/src/overlay/overlay_ui.cpp`, so those characters do not depend on fonts
installed on the player's computer.

The subsets were generated from the Google Fonts variable font sources in the
Noto Project repositories, at weight 600 (Arabic width axis 100), using FontTools.
Only glyphs needed by the overlay's and launcher's localized strings are retained. The original
variable fonts are not included. The corresponding SIL Open Font License 1.1
texts are included here as `OFL-JP.txt`, `OFL-KR.txt`, `OFL-SC.txt`, `OFL-Sans.txt`, and `OFL-Arabic.txt`.

The 2026-11-20 refresh uses the authoritative `google/fonts` repository pinned to
[`cc5661d43cd4f9aad5567033a8e15ca28dc35c86`](https://github.com/google/fonts/tree/cc5661d43cd4f9aad5567033a8e15ca28dc35c86).
Full inputs stay outside the repository. The exact source files and SHA-256 values are:

| Menu subset | Source under `ofl/` at the pinned commit | Source SHA-256 |
|---|---|---|
| `NotoSansJP-menu.ttf` | `notosansjp/NotoSansJP[wght].ttf` | `c2f3b4d463500a2ddcd3849cded1fceeb9fd6d1c32e6cbecd568453ba50fc68f` |
| `NotoSansKR-menu.ttf` | `notosanskr/NotoSansKR[wght].ttf` | `194018e6b2b293a7964f037b25c0249ce1418bc9ab3c971060a03aa57861e252` |
| `NotoSansSC-menu.ttf` | `notosanssc/NotoSansSC[wght].ttf` | `a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da` |
| `NotoSans-menu.ttf` | `notosans/NotoSans[wdth,wght].ttf` | `bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d` |
| `NotoSansArabic-menu.ttf` | `notosansarabic/NotoSansArabic[wdth,wght].ttf` | `63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e` |

Generation uses FontTools 4.68.0. Retain each existing subset's Unicode cmap plus
the translated caption codepoints supported by that family's full source; keep
layout features and names, subset before instancing, then instantiate `wght=600`
and `wdth=100` where present. No variable font axes remain in the menu subsets.
The corresponding authoritative OFL texts match the retained notices apart from
line endings and trailing whitespace. No Microsoft or other system fonts are redistributed.

`tests/overlay_font_coverage_test.py` checks both the atlas ranges and the bundled
font cmaps. It decodes C++ Unicode escapes and includes CJK punctuation, so the
localized Controls captions cannot silently pass as ASCII escape sequences.
Coverage is a character-availability check, not certification of shaping or layout
for every language.

Sources:

- Noto Sans JP: <https://github.com/google/fonts/tree/main/ofl/notosansjp>
- Noto Sans KR: <https://github.com/google/fonts/tree/main/ofl/notosanskr>
- Noto Sans SC: <https://github.com/google/fonts/tree/main/ofl/notosanssc>
- Noto Sans: <https://github.com/google/fonts/tree/main/ofl/notosans>
- Noto Sans Arabic: <https://github.com/google/fonts/tree/main/ofl/notosansarabic>

