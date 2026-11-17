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

Sources:

- Noto Sans JP: <https://github.com/google/fonts/tree/main/ofl/notosansjp>
- Noto Sans KR: <https://github.com/google/fonts/tree/main/ofl/notosanskr>
- Noto Sans SC: <https://github.com/google/fonts/tree/main/ofl/notosanssc>
- Noto Sans: <https://github.com/google/fonts/tree/main/ofl/notosans>
- Noto Sans Arabic: <https://github.com/google/fonts/tree/main/ofl/notosansarabic>

