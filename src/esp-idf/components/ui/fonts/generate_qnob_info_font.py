from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


FONT_PATH = Path("managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf")
OUT_PATH = Path("components/ui/fonts/lv_font_montserrat_64_info.c")
FONT_SIZE = 64
FONT_NAME = "lv_font_montserrat_64_info"
CHARS = " -0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def pack_alpha(img):
    pixels = list(img.getdata())
    out = bytearray()
    for i in range(0, len(pixels), 2):
        hi = pixels[i] >> 4
        lo = (pixels[i + 1] >> 4) if i + 1 < len(pixels) else 0
        out.append((hi << 4) | lo)
    return out


def main():
    font = ImageFont.truetype(str(FONT_PATH), FONT_SIZE)
    ascent, descent = font.getmetrics()

    bitmap = bytearray()
    glyphs = []

    for ch in CHARS:
        adv = int(round(font.getlength(ch)))
        if ch == " ":
            glyphs.append((ord(ch), len(bitmap), adv, 0, 0, 0, 0, ch))
            continue

        bbox = font.getbbox(ch, anchor="ls")
        box_w = max(0, bbox[2] - bbox[0])
        box_h = max(0, bbox[3] - bbox[1])
        if box_w == 0 or box_h == 0:
            glyphs.append((ord(ch), len(bitmap), adv, 0, 0, 0, 0, ch))
            continue

        img = Image.new("L", (box_w, box_h), 0)
        draw = ImageDraw.Draw(img)
        draw.text((-bbox[0], -bbox[1]), ch, font=font, fill=255, anchor="ls")
        idx = len(bitmap)
        bitmap.extend(pack_alpha(img))
        glyphs.append((ord(ch), idx, adv, box_w, box_h, bbox[0], -bbox[3], ch))

    lines = [
        "/* Generated from Montserrat-Medium.ttf for QNOB info display. */",
        '#include "lvgl.h"',
        "",
        f"static const uint8_t {FONT_NAME}_bitmap[] = {{",
    ]

    for i in range(0, len(bitmap), 12):
        chunk = bitmap[i : i + 12]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")

    lines.extend(
        [
            "};",
            "",
            "typedef struct {",
            "    uint32_t unicode;",
            "    uint32_t bitmap_index;",
            "    uint16_t adv_w;",
            "    uint16_t box_w;",
            "    uint16_t box_h;",
            "    int16_t ofs_x;",
            "    int16_t ofs_y;",
            "} qnob_info_glyph_dsc_t;",
            "",
            f"static const qnob_info_glyph_dsc_t {FONT_NAME}_glyphs[] = {{",
        ]
    )

    for code, idx, adv, box_w, box_h, ofs_x, ofs_y, ch in glyphs:
        lines.append(
            f"    {{0x{code:04X}, {idx}, {adv}, {box_w}, {box_h}, {ofs_x}, {ofs_y}}}, /* '{ch}' */"
        )

    lines.extend(
        [
            "};",
            "",
            f"static const qnob_info_glyph_dsc_t* {FONT_NAME}_find(uint32_t letter) {{",
            f"    for (uint32_t i = 0; i < sizeof({FONT_NAME}_glyphs) / sizeof({FONT_NAME}_glyphs[0]); ++i) {{",
            f"        if ({FONT_NAME}_glyphs[i].unicode == letter) return &{FONT_NAME}_glyphs[i];",
            "    }",
            "    return NULL;",
            "}",
            "",
            f"static bool {FONT_NAME}_get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out, uint32_t letter, uint32_t letter_next) {{",
            "    (void)font;",
            "    (void)letter_next;",
            f"    const qnob_info_glyph_dsc_t* g = {FONT_NAME}_find(letter);",
            "    if (g == NULL) return false;",
            "    dsc_out->adv_w = g->adv_w;",
            "    dsc_out->box_w = g->box_w;",
            "    dsc_out->box_h = g->box_h;",
            "    dsc_out->ofs_x = g->ofs_x;",
            "    dsc_out->ofs_y = g->ofs_y;",
            "    dsc_out->bpp = 4;",
            "    dsc_out->is_placeholder = 0;",
            "    return true;",
            "}",
            "",
            f"static const uint8_t* {FONT_NAME}_get_glyph_bitmap(const lv_font_t* font, uint32_t letter) {{",
            "    (void)font;",
            f"    const qnob_info_glyph_dsc_t* g = {FONT_NAME}_find(letter);",
            "    if (g == NULL || g->box_w == 0 || g->box_h == 0) return NULL;",
            f"    return &{FONT_NAME}_bitmap[g->bitmap_index];",
            "}",
            "",
            f"const lv_font_t {FONT_NAME} = {{",
            f"    .get_glyph_dsc = {FONT_NAME}_get_glyph_dsc,",
            f"    .get_glyph_bitmap = {FONT_NAME}_get_glyph_bitmap,",
            f"    .line_height = {ascent + descent},",
            f"    .base_line = {descent},",
            "    .subpx = LV_FONT_SUBPX_NONE,",
            "    .underline_position = -2,",
            "    .underline_thickness = 2,",
            "    .dsc = NULL,",
            "    .fallback = NULL,",
            "};",
        ]
    )

    OUT_PATH.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Wrote {OUT_PATH} ({len(bitmap)} bitmap bytes, {len(glyphs)} glyphs)")


if __name__ == "__main__":
    main()
