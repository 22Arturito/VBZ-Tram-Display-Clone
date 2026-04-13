#!/usr/bin/env python3
"""
VBZ Tram Display — texture generator for Fusion 360 renders.

Two render modes — set RENDER_MODE below:
  "departures"  →  live departure board (uses real vbzfont.h)
  "clock"       →  clock / weather screensaver

Usage:
    pip install pillow
    python3 generate_texture.py
"""

import re
import os
from PIL import Image

# ── Render mode ────────────────────────────────────────────────────────────────
RENDER_MODE = "clock"    # "departures"  or  "clock"

# ── Paths ──────────────────────────────────────────────────────────────────────
FONT_PATH     = "firmware/lib/Display/src/vbzfont.h"
GFX_FONT_PATH = "firmware/.pio/libdeps/freenove_esp32_s3_wroom/Adafruit GFX Library/glcdfont.c"

# ── Display dimensions (matches firmware) ──────────────────────────────────────
W, H = 192, 64

# ── Layout constants (from Display.cpp) ────────────────────────────────────────
DEST_START_X   = 27
ACCESS_X       = W - 31   # 161
TTA_AREA_X     = W - 20   # 172
TTA_AREA_W     = 16
LIVE_MARKER_X  = W - 3    # 189
BADGE_W        = 24
BADGE_H        = 11
ROW_SPACING    = 13

# ── VBZ line colors ─────────────────────────────────────────────────────────────
BG_COLORS = {
    "2":  (229, 0,   0),
    "3":  (0,   138, 41),
    "4":  (14,  37,  110),
    "5":  (116, 69,  30),
    "6":  (204, 128, 52),
    "7":  (30,  30,  30),
    "8":  (137, 183, 0),
    "9":  (15,  38,  113),
    "10": (228, 29,  113),
    "11": (0,   138, 41),
    "13": (255, 194, 0),
    "14": (0,   140, 199),
    "15": (228, 0,   0),
    "17": (144, 29,  77),
}
DARK_TEXT_LINES = {"8", "13"}

# ── Departure data ─────────────────────────────────────────────────────────────
# (line_number, destination, minutes, live_data, is_late, accessible)
DEPARTURES = [
    ("4",  "Hardplatz",    2,  True,  False, True),
    ("7",  "Stettbach",    5,  True,  False, False),
    ("11", "Rehalp",       8,  False, False, True),
    ("13", "Franzensbad",  11, True,  True,  False),
    ("14", "Seebach",      15, False, False, True),
]

# ── Clock / weather data ───────────────────────────────────────────────────────
CLOCK_TIME = "14:32:07"
CLOCK_DATE = "Mo. 13.04.2026"
CLOCK_WX   = "Partly Cloudy  12 C"
CLOCK_UV   = 4      # 0–10
CLOCK_RAIN = [      # mm/h for each of the next 24 hours
    0, 0, 0.2, 0.8, 1.5, 2.1, 3.0, 2.4,
    1.8, 1.2, 0.8, 0.4, 0.1, 0,   0,   0,
    0.3, 0.9, 1.4, 0.7, 0.2, 0,   0,   0,
]

# ── P3 LED pixel parameters ────────────────────────────────────────────────────
LED_PX     = 4
GAP_PX     = 2
SCALE      = LED_PX + GAP_PX
BRIGHTNESS = 0.75
CORNER_MASK = {(0, 0), (LED_PX-1, 0), (0, LED_PX-1), (LED_PX-1, LED_PX-1)}
PANEL_WIDTH = 64
SEAM_PX    = 1


# ═══════════════════════════════════════════════════════════════════════════════
# Font parser (departure board — vbzfont.h)
# ═══════════════════════════════════════════════════════════════════════════════

def parse_font(path):
    with open(path) as f:
        content = f.read()

    bm_match = re.search(r'vbzfontBitmap\[\] PROGMEM = \{(.*?)\};', content, re.DOTALL)
    bm_content = re.sub(r'/\*.*?\*/', '', bm_match.group(1), flags=re.DOTALL)
    bitmap = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', bm_content)]

    gl_match = re.search(r'vbzfontGlyph\[\] PROGMEM = \{(.*?)\};', content, re.DOTALL)
    glyphs = []
    for m in re.finditer(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}',
                         gl_match.group(1)):
        glyphs.append(tuple(int(x) for x in m.groups()))
    return bitmap, glyphs


# ═══════════════════════════════════════════════════════════════════════════════
# vbzfont renderer (departure board)
# ═══════════════════════════════════════════════════════════════════════════════

def glyph_width(code, glyphs):
    if code >= len(glyphs):
        return 6
    return glyphs[code][3]

def measure(text, glyphs):
    return sum(glyph_width(ord(c), glyphs) for c in text)

def draw_char(pixels, cx, cy, code, color, bitmap, glyphs):
    if code >= len(glyphs):
        return 6
    offset, width, height, x_advance, x_off, y_off = glyphs[code]
    if height == 0:
        return x_advance
    bytes_per_row = (width + 7) // 8
    for row in range(height):
        for col in range(width):
            byte_idx = offset + row * bytes_per_row + col // 8
            bit_mask = 0x80 >> (col % 8)
            if byte_idx < len(bitmap) and bitmap[byte_idx] & bit_mask:
                px = cx + x_off + col
                py = cy + y_off + row
                if 0 <= px < W and 0 <= py < H:
                    pixels[py][px] = color
    return x_advance

def draw_text(pixels, x, y, text, color, bitmap, glyphs):
    cx = x
    for ch in text:
        cx += draw_char(pixels, cx, y, ord(ch), color, bitmap, glyphs)
    return cx

def draw_text_right(pixels, right_edge, y, text, color, bitmap, glyphs):
    x = right_edge - measure(text, glyphs)
    draw_text(pixels, x, y, text, color, bitmap, glyphs)


# ═══════════════════════════════════════════════════════════════════════════════
# Departure row renderer
# ═══════════════════════════════════════════════════════════════════════════════

def fill_rect(pixels, x0, y0, x1, y1, color):
    for y in range(max(0, y0), min(H, y1 + 1)):
        for x in range(max(0, x0), min(W, x1 + 1)):
            pixels[y][x] = color

def draw_row(pixels, bitmap, glyphs, line, dest, ttl, live, late, acc, row):
    y = row * ROW_SPACING
    bg = BG_COLORS.get(line, (40, 40, 40))
    fill_rect(pixels, 0, y, BADGE_W - 1, y + BADGE_H - 1, bg)
    badge_text_color = (0, 0, 0) if line in DARK_TEXT_LINES else (255, 255, 255)
    draw_text_right(pixels, 23, y, line, badge_text_color, bitmap, glyphs)
    text_col = (255, 255, 255)
    dest_mapped = dest.replace("ä", "{").replace("ö", "|").replace("ü", "}")
    draw_text(pixels, DEST_START_X, y, dest_mapped, text_col, bitmap, glyphs)
    if acc:
        draw_char(pixels, ACCESS_X, y, 0x1F, text_col, bitmap, glyphs)
    if ttl == 0:
        draw_char(pixels, TTA_AREA_X + TTA_AREA_W - 8, y, 0x1E, text_col, bitmap, glyphs)
    else:
        ttl_str = f">{ttl}" if late else str(ttl)
        draw_text_right(pixels, TTA_AREA_X + TTA_AREA_W, y, ttl_str, text_col, bitmap, glyphs)
        marker = "`" if live else "'"
        draw_char(pixels, LIVE_MARKER_X, y, ord(marker), text_col, bitmap, glyphs)


# ═══════════════════════════════════════════════════════════════════════════════
# GFX default 5×7 font parser + renderer (clock screen)
# ═══════════════════════════════════════════════════════════════════════════════

def parse_gfx_font(path):
    """Extract byte array from glcdfont.c."""
    with open(path) as f:
        content = f.read()
    match = re.search(r'font\[\] PROGMEM = \{(.*?)\};', content, re.DOTALL)
    return [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', match.group(1))]


def draw_gfx_char(pixels, x, y, char_code, color, gfx_font, size=1):
    """Draw one character using GFX default font at the given size scale."""
    for i in range(5):
        idx = char_code * 5 + i
        if idx >= len(gfx_font):
            continue
        line = gfx_font[idx]
        for j in range(8):
            if line & (1 << j):
                for sy in range(size):
                    for sx in range(size):
                        px = x + i * size + sx
                        py = y + j * size + sy
                        if 0 <= px < W and 0 <= py < H:
                            pixels[py][px] = color


def draw_gfx_text(pixels, x, y, text, color, gfx_font, size=1):
    """Draw a string using GFX default font. Advance = 6 * size per char."""
    cx = x
    for ch in text:
        draw_gfx_char(pixels, cx, y, ord(ch), color, gfx_font, size)
        cx += 6 * size
    return cx


def measure_gfx(text, size=1):
    return len(text) * 6 * size


# ═══════════════════════════════════════════════════════════════════════════════
# Clock / weather renderer
# ═══════════════════════════════════════════════════════════════════════════════

def draw_clock(pixels, gfx_font):
    """Render the clock / weather screensaver (matches Display::showClock)."""
    WHITE = (255, 255, 255)
    GRAY  = (180, 180, 180)

    # ── Layout (from Display.cpp showClock) ────────────────────────────────────
    time_h = 16; date_h = 8; wx_h = 8; uv_h = 4; gap = 4
    rain_diag_h = 15
    clock_area  = H - rain_diag_h          # 49px
    total_h     = time_h + gap + date_h + gap + wx_h + gap + uv_h   # 48px
    y = max(1, (clock_area - total_h) // 2)

    # Time "HH:MM:SS" at GFX size 2 (12px advance, 16px tall)
    tw = measure_gfx(CLOCK_TIME, size=2)
    draw_gfx_text(pixels, (W - tw) // 2, y, CLOCK_TIME, WHITE, gfx_font, size=2)
    y += time_h + gap

    # Date at GFX size 1 (6px advance, 8px tall)
    dw = measure_gfx(CLOCK_DATE, size=1)
    draw_gfx_text(pixels, (W - dw) // 2, y, CLOCK_DATE, WHITE, gfx_font, size=1)
    y += date_h + gap

    # Weather string at GFX size 1
    ww = measure_gfx(CLOCK_WX, size=1)
    draw_gfx_text(pixels, (W - ww) // 2, y, CLOCK_WX, GRAY, gfx_font, size=1)
    y += wx_h + gap

    # UV colour bar — 10 segments × (8px + 2px gap) = 98px total
    SEG_W, SEG_GAP = 8, 2
    bar_w = 10 * SEG_W + 9 * SEG_GAP
    bar_x = (W - bar_w) // 2
    for i in range(10):
        x0 = bar_x + i * (SEG_W + SEG_GAP)
        if i < CLOCK_UV:
            if   i < 2: seg_col = (0,   180, 0)
            elif i < 5: seg_col = (220, 220, 0)
            elif i < 7: seg_col = (255, 100, 0)
            else:       seg_col = (220, 0,   0)
        else:
            seg_col = (35, 35, 35)
        fill_rect(pixels, x0, y, x0 + SEG_W - 1, y + uv_h - 1, seg_col)

    # 24h rain chart at bottom (y=49…62, 14px effective height)
    SLOT_W = 6; BAR_W = 3; MAX_MMH = 5.0
    rain_y = H - rain_diag_h          # 49
    y_bot  = H - 2                    # 62
    eff_h  = y_bot - rain_y + 1       # 14px
    diag_x = 1 + ((W - 2) - 24 * SLOT_W) // 2
    BLUE   = (30, 110, 220)
    STUB   = (22, 22, 22)

    for idx, val in enumerate(CLOCK_RAIN[:24]):
        bx = diag_x + idx * SLOT_W
        if val < 0.05:
            fill_rect(pixels, bx, y_bot, bx + BAR_W - 1, y_bot, STUB)
        else:
            bar_h = min(eff_h, max(1, int(val * eff_h / MAX_MMH + 0.5)))
            fill_rect(pixels, bx, y_bot - bar_h + 1, bx + BAR_W - 1, y_bot, BLUE)

    # 6h tick marks
    TICK = (200, 200, 200)
    for g in range(4):
        tx = diag_x + g * 6 * SLOT_W - 2
        fill_rect(pixels, tx, y_bot - 2, tx, y_bot, TICK)


# ═══════════════════════════════════════════════════════════════════════════════
# P3 LED upscale
# ═══════════════════════════════════════════════════════════════════════════════

def apply_led_effect(fb_pixels):
    n_seams = W // PANEL_WIDTH - 1
    out_w   = W * SCALE + n_seams * SEAM_PX
    out_h   = H * SCALE
    out     = Image.new("RGB", (out_w, out_h), (0, 0, 0))
    opx     = out.load()

    for py in range(H):
        for px in range(W):
            r, g, b = fb_pixels[py][px]
            if not (r or g or b):
                continue
            r = int(r * BRIGHTNESS)
            g = int(g * BRIGHTNESS)
            b = int(b * BRIGHTNESS)
            seams_before = px // PANEL_WIDTH
            x0 = px * SCALE + seams_before * SEAM_PX
            y0 = py * SCALE
            for dy in range(LED_PX):
                for dx in range(LED_PX):
                    if (dx, dy) not in CORNER_MASK:
                        opx[x0 + dx, y0 + dy] = (r, g, b)
    return out


# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    pixels     = [[(0, 0, 0)] * W for _ in range(H)]

    if RENDER_MODE == "clock":
        gfx_font = parse_gfx_font(os.path.join(script_dir, GFX_FONT_PATH))
        draw_clock(pixels, gfx_font)
        out_name = "clock_texture.png"
    else:
        font_path      = os.path.join(script_dir, FONT_PATH)
        bitmap, glyphs = parse_font(font_path)
        for i, (line, dest, ttl, live, late, acc) in enumerate(DEPARTURES[:5]):
            draw_row(pixels, bitmap, glyphs, line, dest, ttl, live, late, acc, i)
        out_name = "display_texture.png"

    out      = apply_led_effect(pixels)
    out_path = os.path.join(script_dir, out_name)
    out.save(out_path)
    print(f"Saved {out_path}  ({out.width}×{out.height} px)")


if __name__ == "__main__":
    main()
