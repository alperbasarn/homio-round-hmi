# Character set rule (D9 / UIX-01)

Verified against the font sources in the tree on 2026-09-12. This replaces the
glyph-coverage notes in both screen-design specs, which claimed the degree sign
is unavailable. It is available.

## What the built-in fonts cover

Every built-in LVGL 8.4 Montserrat face (`lv_font_montserrat_14/20/28/48.c` in
`managed_components/lvgl__lvgl/src/font/`) is generated with
`-r 0x20-0x7F,0xB0,0x2022` plus the FontAwesome symbol subset. Text encoding is
UTF-8 (`CONFIG_LV_TXT_ENC_UTF8=y`). So a label may contain:

| Range | Glyphs |
|---|---|
| U+0020 to U+007F | printable ASCII, including `-` (hyphen-minus), `%`, `/`, `:` |
| U+00B0 | `°` degree sign |
| U+2022 | `•` bullet |
| FontAwesome private use | the `LV_SYMBOL_*` macros: AUDIO VIDEO LIST OK CLOSE POWER SETTINGS HOME DOWNLOAD DRIVE REFRESH MUTE VOLUME_MID VOLUME_MAX IMAGE TINT PREV PLAY PAUSE STOP NEXT EJECT LEFT RIGHT PLUS MINUS EYE_OPEN EYE_CLOSE WARNING SHUFFLE UP DOWN LOOP DIRECTORY UPLOAD CALL CUT COPY SAVE BARS ENVELOPE CHARGE PASTE BELL KEYBOARD GPS FILE WIFI BATTERY_FULL BATTERY_3 BATTERY_2 BATTERY_1 BATTERY_EMPTY USB BLUETOOTH TRASH EDIT BACKSPACE SD_CARD NEW_LINE DUMMY |

Anything else renders as the missing-glyph box. That includes every accented
letter, `±` U+00B1, `×` U+00D7, `−` U+2212, `–` U+2013, `—` U+2014, `…` U+2026,
`'` U+2019, `"` U+201C/201D, and `°F` is fine but `℉` U+2109 is not.

## The rule

1. Temperature strings are `<number>°C` with U+00B0 written literally as UTF-8 in
   the source (`"21.5°C"`, `"-5°C"`). Negative values use ASCII `-`, never U+2212.
2. No en dash, em dash, ellipsis or curly quotes in any string a label shows. Use
   ASCII `-`, `...` or `LV_LABEL_LONG_DOT`, and straight quotes.
3. Icons in text are `LV_SYMBOL_*` macros only; no emoji, no other Unicode symbols.
4. Track titles and artist names from Spotify or the PC are untrusted UTF-8: pass
   them through a filter that replaces any codepoint outside the table above with
   `?` before they reach a label (task for the media view work package).
5. When a face is generated from a custom range (the 96 px numeric face some
   design notes mention), it must include at least 0x20-0x7F and 0xB0.

## Enabling the type ramp

Only Montserrat 14 is enabled today. The winning design's ramp needs
`CONFIG_LV_FONT_MONTSERRAT_20=y`, `_28=y`, `_48=y` (task SCRN-05). Keep the
default font at 14 so a forgotten `lv_obj_set_style_text_font` is visibly wrong
rather than silently large.
